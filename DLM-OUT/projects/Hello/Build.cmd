@echo off
setlocal
pushd "%~dp0"
call "..\..\dlm.cmd" build --trusted-only main.dlm -o Hello.exe
set "DLM_APP_EXIT=%ERRORLEVEL%"
popd
exit /b %DLM_APP_EXIT%
