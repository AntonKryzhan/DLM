@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" -Run %*
set "DPI_RESULT=%ERRORLEVEL%"
if not "%DPI_RESULT%"=="0" pause
exit /b %DPI_RESULT%
