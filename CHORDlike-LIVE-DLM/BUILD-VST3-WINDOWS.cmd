@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "SDK=%~dp0..\DLM-OUT"
if exist "%SDK%\bin\dlm.exe" set "PATH=%SDK%\bin;%SDK%\toolchain\mingw64\bin;%PATH%"
where dlm >nul 2>nul
if errorlevel 1 (echo ERROR: dlm.exe missing; DLM-OUT must be beside this folder.& exit /b 1)
where gcc >nul 2>nul
if errorlevel 1 (echo ERROR: gcc.exe missing; check DLM-OUT\toolchain\mingw64\bin.& exit /b 1)
call build-windows.cmd
if errorlevel 1 exit /b 1
echo DLM check: src\live_editor.dlm
dlm check src\live_editor.dlm --allow-unsafe
if errorlevel 1 exit /b 1
echo Building DLM vector editor DLL...
dlm build src\live_editor.dlm --allow-unsafe --target sharedlib --cpu auto -o dist\windows-x86_64\CHORDlike-LIVE-DLM-UI.dll
if errorlevel 1 exit /b 1
set "BUNDLE=dist\vst3-windows\CHORDlike LIVE.vst3\Contents\x86_64-win"
if not exist "%BUNDLE%" mkdir "%BUNDLE%"
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Werror -shared -Iinclude -o "%BUNDLE%\CHORDlike LIVE.vst3" vst3\bridge.c dist\windows-x86_64\CHORDlike-LIVE-DLM.dll dist\windows-x86_64\CHORDlike-LIVE-DLM-UI.dll -lm -luser32 -lgdi32
if errorlevel 1 exit /b 1
copy /y dist\windows-x86_64\CHORDlike-LIVE-DLM.dll "%BUNDLE%\CHORDlike-LIVE-DLM.dll" >nul
if errorlevel 1 exit /b 1
copy /y dist\windows-x86_64\CHORDlike-LIVE-DLM-UI.dll "%BUNDLE%\CHORDlike-LIVE-DLM-UI.dll" >nul
if errorlevel 1 exit /b 1
for %%F in (libgcc_s_seh-1.dll libwinpthread-1.dll libatomic-1.dll libstdc++-6.dll) do (
  if exist "%%~$PATH:F" copy /y "%%~$PATH:F" "%BUNDLE%\%%F" >nul
)
where py >nul 2>nul
if not errorlevel 1 (
  py -3 tools\validate_vst3_windows.py "%CD%\%BUNDLE%\CHORDlike LIVE.vst3"
  if errorlevel 1 (echo ERROR: Windows VST3 smoke failed.& exit /b 1)
) else (
  echo NOTE: Python launcher not found. Windows smoke test skipped.
)
echo.
echo VST3 created: %CD%\dist\vst3-windows\CHORDlike LIVE.vst3
echo Install the entire folder CHORDlike LIVE.vst3 to C:\Program Files\Common Files\VST3
endlocal
