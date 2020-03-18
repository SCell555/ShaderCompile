# ShaderCompile
Standalone shadercompile, that doesn't depend on valve libraries and supports x64. Also removes dependencies
on external tools (no perl or DxSdk)
## Usage
```
ShaderCompile.exe [OPTIONS] -ver n -shaderdir src_dir shader.fxc
```
## Options
```
-ver ARG                       Sets shader version, required
-shaderpath ARG                Base path for shaders, required
-crc                           Calculate crc for shader
-dynamic                       Generate only header
-force                         Skip crc check during compilation
-threads ARG                   Number of threads used, defaults to core count

-h, -help                      Shows help
-verbose                       Verbose file cache and final shader info
-verbose2                      Verbose compile commands
-verbose_preprocessor          Enables preprocessor debug printing

-disable-optimization, /Od     Disables shader optimization
-disable-preshader, /Op        Disables preshader generation
-no-flow-control, /Gfa         Directs the compiler to not use flow-control constructs where possible
-prefer-flow-control, /Gfp     Directs the compiler to use flow-control constructs where possible
-partial-precision, /Gpp       Compiles shader with partial precission
-no-validation, /Vd            Skips shader validation
```
## Shader model version support
All shader models starting from PS2.b/VS2.0
&NewLine;  
&NewLine;  
Valid options for  `-ver`
```
20          ps2b/vs20
30          ps30/vs30
```
## Getting started
This assumes you have "clean" Source SDK2013 project.
1. In `game_shader_dx9_base.vpc` replace `$AdditionalIncludeDirectories	"$BASE;fxctmp9;vshtmp9;"`
 with `$AdditionalIncludeDirectories	"$BASE;include"` , shader headers will be now located in more sensible place
2. Replace `cshader.h` in public/shaderlib with one from this repo, if you are using VS2013 compiler use the one from
VS2013 folder
3. Place `ShaderCompile.exe` and `process_shaders.ps1` to devtools/bin folder where `vpc.exe` is located
4. Replace `buildshaders.bat` with one from this repo
5. In `buildsdkshaders.bat`, remove from all commands `-dx9_30` so
    ```batch
    %BUILD_SHADER% stdshader_dx9_30 -game %GAMEDIR% -source %SOURCEDIR% -dx9_30 -force30 
    ```
    looks like
    ```batch
    %BUILD_SHADER% stdshader_dx9_30 -game %GAMEDIR% -source %SOURCEDIR% -force30
    ```
6. Optionally remove all perl scripts for compiling shaders from devtools/bin, as they will be never used again
    ```
    buildshaderlist.pl
    checkshaderchecksums.pl
    copyshaderincfiles.pl
    copyshaders.pl
    fxc_prep.pl
    psh_prep.pl
    shaderinfo.pl
    uniqifylist.pl
    updateshaders.pl
    valve_perl_helpers.pl
    vsh_prep.pl
    ```