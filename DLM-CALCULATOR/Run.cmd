@echo off
setlocal
pushd "%~dp0"
if exist "Calculator.exe" goto run
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1"
if errorlevel 1 goto failed
:run
start "" /wait "%~dp0Calculator.exe" %*
set "runCode=%ERRORLEVEL%"
if "%runCode%"=="0" goto done
echo Calculator failed. See calculator-error.txt and build\build.log.
pause
goto done
:failed
set "runCode=1"
echo Build failed. See build\build.log.
pause
:done
popd
exit /b %runCode%
