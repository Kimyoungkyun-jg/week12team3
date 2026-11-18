@echo off
setlocal

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" ^
    -NoProfile ^
    -ExecutionPolicy RemoteSigned ^
    -File "%~dp0RegisterDumpViewer.ps1"

set "REGISTER_RESULT=%ERRORLEVEL%"

if not "%REGISTER_RESULT%"=="0" (
    echo Registration failed. Check the message above.
)

pause
exit /b %REGISTER_RESULT%