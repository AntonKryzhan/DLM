@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-DLM.ps1" %*
set "DLM_BUILD_EXIT=%ERRORLEVEL%"
echo.
if not "%DLM_BUILD_EXIT%"=="0" echo Build failed. See the error and build log above.
pause
exit /b %DLM_BUILD_EXIT%
