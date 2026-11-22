@echo off
setlocal
cd /d "%~dp0"
"%~dp0NoDPI.exe" --config=config.ini %*
set "DPI_RESULT=%ERRORLEVEL%"
pause
exit /b %DPI_RESULT%
