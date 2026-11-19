@echo off
setlocal
if "%~1"=="" exit /b 2
set "HAK_BUILD_ROOT=%~f1"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PYTHON_COMMAND=python3.15"
set "HOST_ARCH=X64"
set "VS2022_PREFIX=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\"
set "NASM_PREFIX=%HAK_BUILD_ROOT%\tools\nasm-2.16.03\"
set "PATH=%NASM_PREFIX%;%PATH%"
for %%I in ("%HAK_BUILD_ROOT%\..\ha") do set "WORKSPACE=%%~fI"
if not exist "%WORKSPACE%\edksetup.bat" exit /b 2
set "EDK_TOOLS_PATH=%WORKSPACE%\BaseTools"
set "BASE_TOOLS_PATH=%EDK_TOOLS_PATH%"
set "EDK_TOOLS_BIN=%EDK_TOOLS_PATH%\Bin\Win64"
set "PATH=%EDK_TOOLS_BIN%;%PATH%"
cd /d "%WORKSPACE%"
call edksetup.bat
if errorlevel 1 exit /b 1
pushd BaseTools
rem MSVC 14.51 diagnoses packed HII GUID address in upstream ImageTool;
rem CopyGuid supports this on the x64 host. Keep all other warnings as errors.
rem GenFfs uses UINT32 alignment masks for its bounded firmware sections.
set "_CL_=/wd4366 /wd4319"
nmake
if errorlevel 1 exit /b 1
set "_CL_="
if "%HAK_AUDIO_TRACE%"=="1" set "_CL_=/DHAK_AUDIO_TRACE=1"
popd
call build -a X64 -b RELEASE -t VS2022 -p OpenCorePkg/HakAudioDriver.dsc -m OpenCorePkg/Staging/AudioDxe/AudioDxe.inf -n 4
exit /b %errorlevel%
