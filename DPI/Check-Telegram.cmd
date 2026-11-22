@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Telegram.ps1" %*
set "DPI_RESULT=%ERRORLEVEL%"
pause
exit /b %DPI_RESULT%
