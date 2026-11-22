@echo off
setlocal
set "DLM_PATH=%~dp0lib"
set "CC=%~dp0toolchain\mingw64\bin\gcc.exe"
if not defined DLM_LIBGNUTLS if exist "%~dp0bin\libgnutls-30.dll" set "DLM_LIBGNUTLS=%~dp0bin\libgnutls-30.dll"
if not defined DLM_LIBHOGWEED if exist "%~dp0bin\libhogweed-6.dll" set "DLM_LIBHOGWEED=%~dp0bin\libhogweed-6.dll"
if not defined DLM_LIBQUIC if exist "%~dp0bin\dlm-quic-1.dll" set "DLM_LIBQUIC=%~dp0bin\dlm-quic-1.dll"
set "PATH=%~dp0bin;%~dp0toolchain\mingw64\bin;%PATH%"
"%~dp0bin\dlm.exe" %*
exit /b %ERRORLEVEL%
