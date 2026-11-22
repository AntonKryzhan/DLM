@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" -TestsOnly %*
set "DPI_RESULT=%ERRORLEVEL%"
pause
exit /b %DPI_RESULT%
