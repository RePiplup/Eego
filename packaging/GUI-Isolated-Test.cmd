@echo off
setlocal
"%~dp0python\python.exe" -I "%~dp0tests\gui_fixture.py" --exe "%~dp0bin\handmadebt.exe" --output-root "%~dp0test-output" --launch --interactive
pause
