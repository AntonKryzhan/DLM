@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" %*
set "buildCode=%ERRORLEVEL%"
echo.
pause
exit /b %buildCode%
