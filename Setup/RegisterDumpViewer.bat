@echo off
setlocal

set "HITORI_SETUP_DIR=%~dp0"

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" ^
    -NoProfile ^
    -ExecutionPolicy RemoteSigned ^
    -Command "$ErrorActionPreference='Stop'; foreach ($name in @('RegisterDumpViewer.ps1','OpenCrash.ps1','GitSource.ps1')) { Unblock-File -LiteralPath (Join-Path $env:HITORI_SETUP_DIR $name) }; & (Join-Path $env:HITORI_SETUP_DIR 'RegisterDumpViewer.ps1')"

set "REGISTER_RESULT=%ERRORLEVEL%"

if not "%REGISTER_RESULT%"=="0" (
    echo Registration failed. Check the message above.
)

pause
exit /b %REGISTER_RESULT%