[CmdletBinding()]
param (
    [Parameter(Mandatory=$true, ValueFromPipeline=$true)][System.IO.FileInfo]$File,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$false)][switch]$Dynamic
)

function ReadFile([Parameter()][System.IO.FileInfo]$file) {
    $out = @()
    $files = @($file.Name)
    $f = $file.OpenText()
    while ($null -ne ($l = $f.ReadLine())) {
        if ($l -match '#include\s+"(.*)"') {
            ($o2, $f2) = ReadFile($file.DirectoryName + "\\" + $Matches[1])
            $out += $o2
            $files += $f2
        } else {
            $out += $l
        }
    }
    $f.Close()

    return ($out, $files)
}

function ParseFile([System.IO.FileInfo]$ffile, [string]$ver) {
    if ([System.IO.Path]::GetFileNameWithoutExtension($ffile.Name) -match '_ps(\d+\w?)$') {
        $not_match = '\[vs\d+\w?\]'
        $sh_match = '\[ps(\d+\w?)\]'
        $is_ps = $true
    } else {
        $not_match = '\[ps\d+\w?\]'
        $sh_match = '\[vs(\d+\w?)\]'
        if ($ver -eq "20b") {
            $ver = "20"
        }
        $is_ps = $false
    }

    function ProcessCombo([string]$regex, [string]$line, $init, [System.Collections.Generic.List[object]]$out) {
        if ($line -match $regex) {
            if ($null -ne $init) {
                $out.Add(@{"name" = $Matches[1]; "minVal" = [int]$Matches[2]; "maxVal" = [int]$Matches[3]; "init" = $init})
            } else {
                $out.Add(@{"name" = $Matches[1]; "minVal" = [int]$Matches[2]; "maxVal" = [int]$Matches[3]})
            }
        }
    }

    function NotOnList($spec, $find) {
        if ($null -eq $spec) {
            return $false
        }
        foreach ($ma in $spec.Matches) {
            if ($find -eq $ma.Groups[1]) {
                return $false
            }
        }
        return $true
    }

    $static = [System.Collections.Generic.List[object]]::new()
    $dynamic = [System.Collections.Generic.List[object]]::new()
    $centroids = [System.Collections.Generic.List[string]]::new()
    $skip = [System.Collections.Generic.List[string]]::new()

    ($lines, $files) = ReadFile $ffile
    foreach ($line in $lines) {
        if ($line -notmatch '^\s*//\s*(STATIC|DYNAMIC|SKIP|CENTROID)\s*:\s*(.*)$') {
            continue
        }
        $group = $Matches[1]
        $group2 = $Matches[2]
        if ($line -match '^\s*$') {
            continue
        } elseif ($line -match '\[XBOX\]') {
            continue
        } elseif ($line -match $not_match) {
            continue
        }

        $spec = Select-String $sh_match -Input $line -AllMatches
        if (NotOnList $spec $ver) {
            continue
        }

        if ($line -match '\[\s*=\s*([^\]]+)\]') {
            $init = $Matches[1]
        } else {
            $init = $null
        }

        switch ($group) {
            "STATIC" { 
                ProcessCombo '^\s*//\s*STATIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)"' $line $init $static
             }
            "DYNAMIC" {
                ProcessCombo '^\s*//\s*DYNAMIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)"' $line $init $dynamic
            }
            "CENTROID" {
                $line -match '^\s*//\s*CENTROID\s*:\s*TEXCOORD(\d+)\s*$' | Out-Null
                $centroids.Add([int]$Matches[1])
            }
            "SKIP" {
                $skip.Add(($group2 -split $sh_match,0,"RegexMatch")[0])
            }
            Default {}
        }
    }
    $mask = 0
    foreach ($c in $centroids) {
        $mask += 1 -shl $c
    }

    return ($static, $dynamic, $skip, $mask, $files, $is_ps)
}

function CheckCrc([System.IO.FileInfo]$srcFile, [string]$name) {
    $file = [System.IO.Path]::Combine($srcFile.DirectoryName, "shaders", "fxc", $name + ".vcs")
    if (-not [System.IO.File]::Exists($file)) {
        return $false
    }
    $f = [System.IO.FileStream]::new($file, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    [void]$f.Seek(6*4, [System.IO.SeekOrigin]::Begin)
    $b = [System.Byte[]]::new(4)
    [void]$f.Read($b, 0, 4)
    $f.Close()
    return [System.BitConverter]::ToUInt32($b, 0) -eq [System.UInt32](& .\ShaderCrc $srcFile)
}

function WriteInclude([System.IO.FileInfo]$srcFile, [string]$baseName, [string]$ver) {
    ($static, $dynamic, $skip, $mask, $files, $isPs) = (ParseFile $srcFile $ver)

    $fStream = [System.IO.File]::Open([System.IO.Path]::Combine($srcFile.DirectoryName, "include", $baseName + ".inc"),
     [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    
    $n = [System.Char]10
    $t = [System.Char]9

    $fWriter = [System.IO.StreamWriter]::new($fStream, [System.Text.UTF8Encoding]::new($false))
    $fWriter.NewLine = $n

    function BitCount([System.UInt32]$val) {
        $val = $val -bor ($val -shr 1)
        $val = $val -bor ($val -shr 2)
        $val = $val -bor ($val -shr 4)
        $val = $val -bor ($val -shr 8)
        $val = $val -bor ($val -shr 16)

        $val -= (($val -shr 1) -band 0x55555555)
        $val = (($val -shr 2) -band 0x33333333) + ($val -band 0x33333333)
        $val = ($val -shr 4) + ($val -band 0x0f0f0f0f)
        $val += ($val -shr 8)
        $val += ($val -shr 16)
        return $val -band 0x3f
    }

    function WriteVars([string]$suffix, [System.Collections.Generic.List[object]]$vars, [string]$ctor, [System.UInt32]$scale) {
        $fWriter.Write("class $($baseName)_$($suffix)_Index$($n){$($n)")
        $writeIfdef = $false
        foreach ($v in $vars) {
            $bits = BitCount($v.maxVal - $v.minVal + 1)
            $fWriter.Write("$($t)int m_n$($v.name) : $bits;$($n)")
            if ($null -eq $v.init) {
                $writeIfdef = $true
            }
        }
        if ($writeIfdef) {
            $fWriter.Write("#ifdef DEBUG$($n)")
        }

        foreach ($v in $vars) {
            if ($null -eq $v.init) {
                $fWriter.Write("$($t)bool m_b$($v.name) : 1;$($n)")
            }
        }

        if ($writeIfdef) {
            $fWriter.Write("#endif$($n)public:$($n)")
        }

        foreach ($v in $vars) {
            $fWriter.Write("$($t)void Set$($v.name)( int i )$($n)$($t){$($n)")
            $fWriter.Write("$($t)$($t)Assert( i >= $($v.minVal) && i <= $($v.maxVal) );$($n)")
            $fWriter.Write("$($t)$($t)m_n$($v.name) = i;$($n)")
            if ($null -eq $v.init) {
                $fWriter.Write("#ifdef DEBUG$($n)")
                $fWriter.Write("$($t)$($t)m_b$($v.name) = true;$($n)")
                $fWriter.Write("#endif$($n)")
            }
            $fWriter.Write("$($t)}$($n)$($n)")
        }
        
        $fWriter.Write("$($t)$($baseName)_$($suffix)_Index( $ctor )$($n)$($t){$($n)")
        foreach ($v in $vars) {
            $fWriter.Write("$($t)$($t)m_n$($v.name) = $(('0', $v.init)[$null -ne $v.init]);$($n)")
        }
        if ($writeIfdef) {
            $fWriter.Write("#ifdef DEBUG$($n)")
        }
        foreach ($v in $vars) {
            if ($null -eq $v.init) {
                $fWriter.Write("$($t)$($t)m_b$($v.name) = false;$($n)")
            }
        }
        if ($writeIfdef) {
            $fWriter.Write("#endif$($n)")
        }
        $fWriter.Write("$($t)}$($n)$($n)")

        $fWriter.Write("$($t)int GetIndex()$($n)$($t){$($n)")
        if ($vars.Count -eq 0) {
            $fWriter.Write("$($t)$($t)return 0;$($n)")
        } else {
            $p = [System.Linq.Enumerable]::Where($vars, [Func[object,bool]]{ param($v) $null -eq $v.init })
            $p = [System.Linq.Enumerable]::Select($p, [Func[object, string]]{ param($c) "m_b" + $c.name })
            $fWriter.Write("$($t)$($t)Assert( $([System.String]::Join(' && ', $p )) );$($n)")
            $fWriter.Write("$($t)$($t)return ")
            foreach ($v in $vars) {
                $fWriter.Write("( $scale * m_n$($v.name) ) + ")
                $scale *= $v.maxVal - $v.minVal + 1
            }
            $fWriter.Write("0;$($n)")
        }
        $fWriter.Write("$($t)}$($n)};$($n)$($n)")

        $fWriter.Write("#define shader$($suffix)Test_$baseName ")
        
        $p = [System.Linq.Enumerable]::Where($vars, [Func[object,bool]]{ param($v) $null -eq $v.init })
        $pref = "psh_"
        if ($isPs -eq $false) {
            $pref = "vsh_"
        }
        $pref += "$($pref)forgot_to_set_$($suffix.ToLower())_"
        $p = [System.Linq.Enumerable]::Select($p, [Func[object, string]]{ param($c) $pref + $c.name })
        $p = [System.Linq.Enumerable]::ToList($p)
        if ($p.Count -ne 0) {
            $fWriter.Write([System.String]::Join(" + ", $p))
            $fWriter.Write("$($n)$($n)")
        } else {
            $fWriter.Write("1$($n)$($n)")
        }
    }

    if ($skip.Count -ne 0) {
        $fWriter.Write("// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!$($n)")
        foreach ($s in $skip) {
            $fWriter.Write("// $s$($n)")
        }
        $fWriter.Write("$($n)")
    }

    $fWriter.Write("#ifndef $($baseName.ToUpperInvariant())_H$($n)#define $($baseName.ToUpperInvariant())_H$($n)$($n)")
    $fWriter.Write("#include ""shaderapi/ishaderapi.h""$($n)#include ""shaderapi/ishadershadow.h""$($n)#include ""materialsystem/imaterialvar.h""$($n)$($n)")

    WriteVars "Static" $static "IShaderShadow* pShaderShadow, IMaterialVar** params" ([System.Linq.Enumerable]::Aggregate($dynamic, [System.UInt32]1, [Func[System.UInt32,object,System.UInt32]]{ param($r, $c) $r * ($c.maxVal - $c.minVal + 1) }))
    $fWriter.Write("$n")
    WriteVars "Dynamic" $dynamic "IShaderDynamicAPI* pShaderAPI" 1

    $fWriter.Write("$($n)#endif$($t)// $($baseName.ToUpperInvariant())_H")

    $fWriter.Close()

    return ($static, $dynamic, $skip, $mask, $files, $isPs)
}

function main() {
    if ($Version -notin @("20b", "30")) {
        return
    }

    $isV3 = $Version -eq "30"
    $outVersion = @("vs_2_0", "vs_3_0", "ps_2_b", "ps_3_0")
    $to_process = @{}

    $fileList = $File.OpenText()
    while ($null -ne ($line = $fileList.ReadLine())) {
        if ($line -match '^\s*$' -or $line -match '^\s*//') {
            continue
        }

        $full = [System.IO.Path]::Combine($File.DirectoryName, $line)
        $name = [System.IO.Path]::GetFileNameWithoutExtension($line) -replace '(_[vp]s)\d+\w?$',('${1}' + $Version)

        if (CheckCrc $full $name) {
            continue
        }

        ($st, $dyn, $skip, $mask, $files, $isPs) = WriteInclude $full $name $Version

        if ($Dynamic) {
            continue
        }

        foreach ($c in $st) {
            $c.Remove("init")
        }

        foreach ($c in $dyn) {
            $c.Remove("init")
        }

        $to_process[$name] = @{"static" = $st; "dynamic" = $dyn; "files" = $files; "centroid" = $mask; "version" = $outVersion[[int]$isV3 + [int]$isPs * 2]; "skip" = "(" + [System.String]::Join(")||(", $skip) + ")" }
    }
    $fileList.Close()

    if ($to_process.Count -ne 0) {
        ConvertTo-Json $to_process -Depth 3 -Compress | Out-File ([System.IO.Path]::Combine($File.DirectoryName, $File.BaseName + "_work.json")) -NoNewline
    }
}

main