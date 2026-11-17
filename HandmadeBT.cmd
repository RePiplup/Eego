@echo off
setlocal
set "EEGO_HOME=%~dp0dist\Eego"
if not exist "%EEGO_HOME%\HandmadeBTQuick.exe" goto missing
if not exist "%EEGO_HOME%\core\task_service_host_r3.exe" goto missing
if not exist "%EEGO_HOME%\Qt6Core.dll" goto missing
if not exist "%EEGO_HOME%\platforms\qwindows.dll" goto missing
start "" /wait "%EEGO_HOME%\HandmadeBTQuick.exe" %*
exit /b %errorlevel%
:missing
echo ERROR: Unified Eego program or required runtime is missing.
echo Expected directory: "%EEGO_HOME%"
echo Restore the verified unified package. No alternate task library was opened.
exit /b 2
