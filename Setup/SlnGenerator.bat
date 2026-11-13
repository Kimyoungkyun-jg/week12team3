@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
"%~dp0premake5.exe" "--file=%~dp0premake5.lua" vs2026 %*
set "RESULT=%ERRORLEVEL%"
popd
if not defined CI pause
exit /b %RESULT%
