@echo off
setlocal
if "%~2"=="" exit /b 2
set "WORKSPACE=%~f1"
set "NASM_PREFIX=%~f2\"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PYTHON_COMMAND=python3.15"
set "HOST_ARCH=X64"
set "VS2022_PREFIX=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\"
set "EDK_TOOLS_PATH=%WORKSPACE%\BaseTools"
set "BASE_TOOLS_PATH=%EDK_TOOLS_PATH%"
set "EDK_TOOLS_BIN=%EDK_TOOLS_PATH%\Bin\Win64"
set "PATH=%NASM_PREFIX%;%EDK_TOOLS_BIN%;%PATH%"
cd /d "%WORKSPACE%"
call edksetup.bat
if errorlevel 1 exit /b 1
call build -a X64 -b RELEASE -t VS2022 -p ShellPkg/CharonShell.dsc -m ShellPkg/Application/CharonShell/Shell.inf -n 4
exit /b %errorlevel%
