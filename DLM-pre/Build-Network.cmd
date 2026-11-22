@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Network.ps1" %*
set "DLM_NETWORK_EXIT=%ERRORLEVEL%"
echo.
pause
exit /b %DLM_NETWORK_EXIT%
