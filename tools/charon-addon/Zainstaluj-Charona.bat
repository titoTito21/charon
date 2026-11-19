@echo off
rem Charon: instalator (czytnik ekranu albo menedzer rozruchu).
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0Install-Charon.ps1\" %*'"
