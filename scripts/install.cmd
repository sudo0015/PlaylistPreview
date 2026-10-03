@echo off
setlocal
rem Thin wrapper: the real logic lives in install.ps1, which elevates itself.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
exit /b %errorlevel%
