@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\open_manager.ps1" -Demo
if errorlevel 1 pause
