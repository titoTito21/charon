@echo off
rem Charon: czytnik ekranu jako dodatek UEFI (Driver####). Windows zostaje domyslny.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0Install-CharonAddon.ps1\" %*'"
