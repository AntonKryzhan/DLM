@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" -TestsOnly %*
set "testCode=%ERRORLEVEL%"
echo.
pause
exit /b %testCode%
