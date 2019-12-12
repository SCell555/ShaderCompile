@echo off

set TTEXE=..\..\devtools\bin\timeprecise.exe
if not exist %TTEXE% goto no_ttexe
goto no_ttexe_end

:no_ttexe
set TTEXE=time /t
:no_ttexe_end

echo.
echo ==================== buildshaders %* ==================
%TTEXE% -cur-Q
set tt_start=%ERRORLEVEL%
set tt_chkpt=%tt_start%


REM ****************
REM usage: buildshaders <shaderProjectName>
REM ****************

setlocal
set arg_filename=%1
set shadercompilecommand=ShaderCompile.exe
set targetdir=shaders
set SrcDirBase=..\..
set shaderDir=shaders
set SDKArgs=-local

if "%1" == "" goto usage
set inputbase=%1

if /i "%6" == "-force30" goto set_force30_arg
:set_force30_arg
			set IS30=1
			goto set_force_end
:set_force_end

if /i "%2" == "-game" goto set_mod_args
goto build_shaders

REM ****************
REM USAGE
REM ****************
:usage
echo.
echo "usage: buildshaders <shaderProjectName> [-game] [gameDir if -game was specified] [-source sourceDir]"
echo "       gameDir is where gameinfo.txt is (where it will store the compiled shaders)."
echo "       sourceDir is where the source code is (where it will find scripts and compilers)."
echo "ex   : buildshaders myshaders"
echo "ex   : buildshaders myshaders -game c:\steam\steamapps\sourcemods\mymod -source c:\mymod\src"
goto :end

REM ****************
REM MOD ARGS - look for -game or the vproject environment variable
REM ****************
:set_mod_args

if not exist "..\..\devtools\bin\ShaderCompile.exe" goto NoShaderCompile
if not exist "..\..\devtools\bin\ShaderCrc.exe" goto NoShaderCrc
set ChangeToDir=%SrcDirBase%\devtools\bin\

if /i "%4" NEQ "-source" goto NoSourceDirSpecified
set SrcDirBase=%~5

if not exist "%~3\gameinfo.txt" goto InvalidGameDirectory
goto build_shaders

REM ****************
REM ERRORS
REM ****************
:InvalidGameDirectory
echo Error: "%~3" is not a valid game directory.
echo (The -game directory must have a gameinfo.txt file)
goto end

:NoSourceDirSpecified
echo ERROR: If you specify -game on the command line, you must specify -source.
goto usage
goto end

:NoShaderCompile
echo - ERROR: ShaderCompile.exe doesn't exist in devtools\bin
goto end

:NoShaderCompile
echo - ERROR: ShaderCrc.exe doesn't exist in devtools\bin
goto end

REM ****************
REM BUILD SHADERS
REM ****************
:build_shaders

rem echo --------------------------------
rem echo %inputbase%
rem echo --------------------------------
REM make sure that target dirs exist
REM files will be built in these targets and copied to their final destination
if not exist include mkdir include
if not exist %shaderDir% mkdir %shaderDir%
if not exist %shaderDir%\fxc mkdir %shaderDir%\fxc
REM Nuke some files that we will add to later.
if exist "%inputbase%_work.json" del /f /q "%inputbase%_work.json"

set SHVER=20b

if defined IS30 (
	set SHVER=30
)

title %1 %SHVER%

echo Building inc files and worklist for %inputbase%...

set DYNAMIC=
if "%dynamic_shaders%" == "1" set DYNAMIC=-d

py "%SrcDirBase%\devtools\bin\prepare_shaders.py" %DYNAMIC% -v %SHVER% "%inputbase%.txt"

REM ****************
REM Execute distributed process on work/build list
REM ****************

set shader_path_cd=%cd%
if exist "%inputbase%_work.json" if not "%dynamic_shaders%" == "1" (
	rem echo Running distributed shader compilation...

	cd /D %ChangeToDir%
	echo %shadercompilecommand% %SDKArgs% -shaderpath "%shader_path_cd:/=\%" -config "%inputbase%_work.json"
	%shadercompilecommand% %SDKArgs% -shaderpath "%shader_path_cd:/=\%" -config "%inputbase%_work.json"
	cd /D %shader_path_cd%
)

REM ****************
REM PC Shader copy
REM Publish the generated files to the output dir using XCOPY
REM This batch file may have been invoked standalone or slaved (master does final smart mirror copy)
REM ****************
:DoXCopy
if not "%dynamic_shaders%" == "1" (
if not exist "%targetdir%" md "%targetdir%"
if not "%targetdir%"=="%shaderDir%" xcopy %shaderDir%\*.* "%targetdir%" /e /y
)
goto end

REM ****************
REM END
REM ****************
:end


%TTEXE% -diff %tt_start%
echo.