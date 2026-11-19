@echo off
rem Charon: odinstalowanie (wpisy firmware i pliki Charona).
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0Install-Charon.ps1\" -Uninstall'"
