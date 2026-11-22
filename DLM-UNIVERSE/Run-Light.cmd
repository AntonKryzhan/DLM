@echo off
call "%~dp0Run.cmd" --count 120 --scale 85 %*
exit /b %ERRORLEVEL%
