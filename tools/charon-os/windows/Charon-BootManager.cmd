@echo off
rem Charon boot manager. Starts Charon-BootManager.ps1 with administrator rights
rem (the EFI system partition and firmware variables need them). "-ExecutionPolicy Bypass"
rem applies to this one PowerShell process only; the policy of the system is not changed.
rem Arguments are passed on, for example: Charon-BootManager.cmd -Console -Language pl
rem The 64-bit PowerShell is used also when this file is started from a 32-bit program.
setlocal
set "CHARON_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "CHARON_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
set "CHARON_SCRIPT=%~dp0Charon-BootManager.ps1"
rem The dialog needs no console window; the text menus (-Console) keep theirs open.
set "CHARON_WINDOW=-WindowStyle Hidden"
echo(%* | findstr /i /c:"-Console" >nul && set "CHARON_WINDOW=-NoExit"
"%CHARON_PS%" -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList ('-NoProfile -ExecutionPolicy Bypass %CHARON_WINDOW% -File \"' + $env:CHARON_SCRIPT + '\" %*')"
