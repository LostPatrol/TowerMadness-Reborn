@rem Open the current original-x86 development baseline with its original menu.
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -Mode menu
if errorlevel 1 pause
