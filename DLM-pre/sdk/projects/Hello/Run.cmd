@echo off
setlocal
if not exist "%~dp0Hello.exe" (
    call "%~dp0Build.cmd"
    if errorlevel 1 exit /b 1
)
"%~dp0Hello.exe" %*
exit /b %ERRORLEVEL%
