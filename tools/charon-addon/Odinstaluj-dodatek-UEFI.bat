@echo off
rem Usuwa wpis Driver#### Charona i pliki dodatku z partycji EFI.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0Install-CharonAddon.ps1\" -Uninstall'"
