from functools import reduce
import getopt
import io
import json
from os import chmod
from pathlib import Path
import re
from stat import S_IREAD, S_IRGRP, S_IROTH, S_IWUSR
from subprocess import Popen, PIPE
import sys

inc_r = re.compile(r'#\s*include\s+"(.*)"')
xbox_reg_r = re.compile(r'\[XBOX\]')
pc_reg_r = re.compile(r'\s*\[PC\]\s*')
blank_r = re.compile(r'^\s*$')
start_r = re.compile(r'^\s*//\s*(STATIC|DYNAMIC|SKIP|CENTROID)\s*:\s*(.*)$')
init_r = re.compile(r'\[\s*=\s*([^\]]+)\]')
static_combo_r = re.compile(r'^\s*//\s*STATIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)"')
dynamic_combo_r = re.compile(r'^\s*//\s*DYNAMIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)"')
centroid_r = re.compile(r'^\s*//\s*CENTROID\s*:\s*TEXCOORD(\d+)\s*$')
comment_r = re.compile(r'^\s*//')


def read_file(file_name):
    out = []
    spl = Path(file_name)
    files = [spl.name]
    base = spl.absolute().parent
    with open(file_name) as file:
        for line in file:
            m = inc_r.match(line)
            if m:
                (o2, f2) = read_file(base / m.group(1))
                out.extend(o2)
                files.extend(f2)
            else:
                out.append(line)
    return out, files


class Combo:
    def __init__(self, name, min_val, max_val, init=None):
        self.name = name
        self.minVal = int(min_val)
        self.maxVal = int(max_val)
        if init:
            self.init = init


def parse_file(file_name, ver):
    raw_name = Path(file_name).stem
    ps = re.search(r'_ps(\d+\w?)$', raw_name)
    vs = re.search(r'_vs(\d+\w?)$', raw_name)
    if ps:
        not_match = re.compile(r'\[vs\d+\w?\]')
        sh_match = re.compile(r'\[ps(\d+\w?)\]')
    else:
        assert vs
        not_match = re.compile(r'\[ps\d+\w?\]')
        sh_match = re.compile(r'\[vs(\d+\w?)\]')

    def process_combo(regex, line, _init, out):
        c = regex.match(pc_reg_r.sub('', line))
        if _init:
            out.append(Combo(c.group(1), c.group(2), c.group(3), _init.group(1)))
        else:
            out.append(Combo(c.group(1), c.group(2), c.group(3)))

    static = []
    dynamic = []
    skip = []
    mask = 0

    (lines, files) = read_file(file_name)
    for line in lines:
        line_match = start_r.match(line)
        if not line_match:
            continue
        if blank_r.match(line):
            continue
        if xbox_reg_r.search(line):
            continue
        elif not_match.search(line):
            continue
        spec = sh_match.findall(line)
        if len(spec) > 0 and ver not in [s.lower() for s in spec]:
            continue
        init_val = init_r.search(line)
        if line_match.group(1) == 'STATIC':
            process_combo(static_combo_r, line, init_val, static)
        elif line_match.group(1) == 'DYNAMIC':
            process_combo(dynamic_combo_r, line, init_val, dynamic)
        elif line_match.group(1) == "CENTROID":
            mask |= 1 << int(pc_reg_r.sub('', centroid_r.match(line).group(1)))
        else:
            skip.append(pc_reg_r.sub('', sh_match.split(line_match.group(2))[0]).strip())

    return static, dynamic, skip, mask, files


def check_crc(src_file, name):
    file = Path(src_file).parent / 'shaders' / 'fxc' / (name + '.vcs')
    try:
        with open(file, 'rb') as f:
            f.seek(6 * 4, io.SEEK_SET)
            return int.from_bytes(f.read(4), 'little') == int(
                Popen([Path(__file__).resolve().parent / 'ShaderCrc', src_file], stdout=PIPE).communicate()[0])
    except FileNotFoundError:
        return False


def write_include(file_name, base_name, ver, is_vs):
    (static, dynamic, skip, mask, files) = parse_file(file_name, ver)
    name = Path(file_name).parent / 'include' / (base_name + '.inc')
    if name.exists():
        chmod(name, S_IWUSR | S_IREAD)
    with open(name, 'w', newline='\n') as include:
        def write_vars(suffix: str, vars, ctor: str, scale: int):
            include.write('class %s_%s_Index\n{\n' % (base_name, suffix))
            write_ifdef = len([c for c in vars if not hasattr(c, 'init')]) > 0
            for v in vars:
                include.write('\tint m_n%s : %d;\n' % (v.name, (v.maxVal - v.minVal + 1).bit_length()))
            if write_ifdef:
                include.write('#ifdef _DEBUG\n')
            for v in vars:
                if not hasattr(v, 'init'):
                    include.write('\tbool m_b%s : 1;\n' % v.name)
            if write_ifdef:
                include.write('#endif\t// _DEBUG\n')
            include.write('public:\n')
            # setters
            for v in vars:
                include.write('\tvoid Set%s( int i )\n\t{\n' % v.name)
                include.write('\t\tAssert( i >= %d && i <= %d );\n' % (v.minVal, v.maxVal))
                include.write('\t\tm_n%s = i;\n' % v.name)
                if not hasattr(v, 'init'):
                    include.write('#ifdef _DEBUG\n')
                    include.write('\t\tm_b%s = true;\n' % v.name)
                    include.write('#endif\t// _DEBUG\n')
                include.write('\t}\n\n')

            # ctor
            include.write('\t%s_%s_Index( %s )\n\t{\n' % (base_name, suffix, ctor))
            for v in vars:
                include.write('\t\tm_n%s = %s;\n' % (v.name, getattr(v, 'init', '0')))
            if write_ifdef:
                include.write('#ifdef _DEBUG\n')
            for v in vars:
                if not hasattr(v, 'init'):
                    include.write('\t\tm_b%s = false;\n' % v.name)
            if write_ifdef:
                include.write('#endif\t// _DEBUG\n')
            include.write('\t}\n\n')

            # index
            include.write('\tint GetIndex()\n\t{\n')
            if len(vars) == 0:
                include.write('\t\treturn 0;\n')
            else:
                if write_ifdef:
                    include.write(
                        '\t\tAssert( %s );\n' % ' && '.join(['m_b' + v.name for v in vars if not hasattr(v, 'init')]))
                include.write('\t\treturn ')
                for v in vars:
                    include.write('( %d * m_n%s ) + ' % (scale, v.name))
                    scale *= v.maxVal - v.minVal + 1
                include.write('0;\n')
            include.write('\t}\n')
            include.write('};\n\n')

            if is_vs:
                pref = 'vsh_'
            else:
                pref = 'psh_'
            pref += 'forgot_to_set_%s_' % suffix.lower()
            include.write('#define shader%sTest_%s '%(suffix, base_name))
            if write_ifdef:
                include.write(' + '.join([pref + c.name for c in vars if not hasattr(c, 'init')]))
                include.write('\n\n')
            else:
                include.write('1\n\n')

        # skips
        if len(skip) > 0:
            include.write('// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!\n')
            for s in skip:
                include.write('// %s\n' % s)
            include.write('\n')

        include.writelines(['#ifndef %s_H\n' % base_name.upper(), '#define %s_H\n\n' % base_name.upper()])
        include.writelines(['#include "shaderapi/ishaderapi.h"\n', '#include "shaderapi/ishadershadow.h"\n',
                            '#include "materialsystem/imaterialvar.h"\n\n'])  # includes

        # static combos
        write_vars('Static', static, 'IShaderShadow* pShaderShadow, IMaterialVar** params',
                   reduce(lambda v, c: v * (c.maxVal - c.minVal + 1), dynamic, 1))

        include.write('\n')
        # dynamic combos
        write_vars('Dynamic', dynamic, 'IShaderDynamicAPI* pShaderAPI', 1)
        include.write('\n#endif\t// %s_H' % base_name.upper())
    chmod(name, S_IREAD | S_IRGRP | S_IROTH)
    return static, dynamic, skip, mask, files


def main(argv):
    opts, args = getopt.getopt(argv, 'v:d')
    if len(args) == 0:
        assert False, 'no file specified'
    if '-v' not in [o for o, _ in opts]:
        assert False, 'no version specified'

    ver = None
    for s in ['20b', '30']:
        if s in [v for _, v in opts]:
            ver = s
            break
    if ver is None:
        assert False, 'unsupported version specified'
    is_v3 = ver == '30'
    out_versions = ['vs_2_0', 'vs_3_0', 'ps_2_b', 'ps_3_0']

    do_dynamic = ('-d', '') in opts
    file = Path(args[0]).absolute()
    dir_name = file.parent
    with open(file, "r") as file_list:
        to_process = {}
        for li in file_list:
            if blank_r.match(li) or comment_r.match(li):
                continue

            full = dir_name / li.strip(' \n\t')
            is_vs = re.search(r'_vs\d+\w?$', full.stem) is not None
            loc_ver = ver
            if is_vs and ver == "20b":
                loc_ver = "20"
            name = re.sub(r'_[vp]s(\d+\w?)$', lambda m: '%s%s' % (m.group(0)[:3], loc_ver), full.stem.lower())

            if check_crc(full, name):
                continue

            (static, dynamic, skip, mask, files) = write_include(full, name, loc_ver, is_vs)
            if do_dynamic:
                continue

            # ShaderCompile does not need initial values
            [delattr(c, 'init') for c in static + dynamic if hasattr(c, 'init')]
            to_process[name] = {"static": static, "dynamic": dynamic, "files": files, "centroid": mask,
                                "version": out_versions[is_v3 + (not is_vs) * 2], "skip": '(' + ')||('.join(skip) + ')'}

    if len(to_process) > 0:
        with open(dir_name / (file.stem + '_work.json'), "w") as work_list:
            json.dump(to_process, work_list, default=lambda o: o.__dict__)


if __name__ == "__main__":
    main(sys.argv[1:])
