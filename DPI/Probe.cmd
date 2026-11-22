@echo off
setlocal
cd /d "%~dp0"
"%~dp0NoDPIProbe.exe" %*
set "DPI_RESULT=%ERRORLEVEL%"
pause
exit /b %DPI_RESULT%
