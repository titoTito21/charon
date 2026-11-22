@echo off
rem Charon: czytnik ekranu jako dodatek UEFI (Driver####). Windows zostaje domyslny.
rem r408: the 64-bit PowerShell also when started from a 32-bit program (Sysnative); the path of the script
rem goes through the environment, so an apostrophe or a space in the folder name does no harm.
setlocal
set "CHARON_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "CHARON_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
set "CHARON_SCRIPT=%~dp0Install-CharonAddon.ps1"
"%CHARON_PS%" -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -NoExit -File \"' + $env:CHARON_SCRIPT + '\" %*')"
