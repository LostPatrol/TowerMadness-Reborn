@rem Open only the isolated original-code prototype, never the existing product.
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -Mode play
