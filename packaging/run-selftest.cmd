@echo off
setlocal
"%~dp0python\python.exe" -I "%~dp0selftest.py" %*
set "result=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %result%
