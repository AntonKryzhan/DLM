@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0NoDPIGui.exe" (
 echo NoDPIGui.exe not found. Run Build.cmd.
 pause
 exit /b 1
)
start "" "%~dp0NoDPIGui.exe"
