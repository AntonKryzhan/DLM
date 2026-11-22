@echo off
setlocal
cd /d "%~dp0"
where dlm >nul 2>nul
if errorlevel 1 (
  echo DLM v0.133.0 or newer must be available in PATH.
  exit /b 1
)
if not exist "dist\windows-x86_64" mkdir "dist\windows-x86_64"
dlm check "src\live_core.dlm" --allow-unsafe
if errorlevel 1 exit /b 1
dlm build "src\live_core.dlm" --allow-unsafe --target sharedlib --cpu auto -o "dist\windows-x86_64\CHORDlike-LIVE-DLM.dll"
if errorlevel 1 exit /b 1
echo Native DLM LIVE processing library built.
echo This DLL is NOT a VST3 bundle and must not be renamed to .vst3.
