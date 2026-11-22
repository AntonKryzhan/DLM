@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Network.ps1"
set "result=%errorlevel%"
pause
exit /b %result%
