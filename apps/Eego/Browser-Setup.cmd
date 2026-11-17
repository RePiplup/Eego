@echo off
setlocal
if not exist "%~dp0browser\extension\manifest.json" exit /b 1
if not exist "%~dp0eego_browser_host.exe" exit /b 1
for %%B in ("Microsoft\Edge" "Google\Chrome") do (
  reg add "HKCU\Software\%%~B\NativeMessagingHosts\org.handmadebt.bridge" /ve /t REG_SZ /d "%~dp0browser\native-host.json" /f || exit /b 1
)
echo Open edge://extensions or chrome://extensions and load this unpacked extension:
echo %~dp0browser\extension
echo Enable download capture in the extension popup.
