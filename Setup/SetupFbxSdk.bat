@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupFbxSdk.ps1" %*
set "SetupExitCode=%ERRORLEVEL%"
echo.
pause
exit /b %SetupExitCode%
