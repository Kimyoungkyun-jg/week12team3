@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "HITORI_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "HITORI_REGISTER_SCRIPT=%~dp0RegisterDumpViewer.ps1"
set "HITORI_OPEN_SCRIPT=%~dp0OpenCrash.ps1"
set "HITORI_GIT_SCRIPT=%~dp0GitSource.ps1"
set "HITORI_SETTINGS_FILE=%~dp0HitoriCrash.vssettings"

if not exist "%HITORI_PS%" goto :missing_powershell
if not exist "%HITORI_REGISTER_SCRIPT%" goto :missing_files
if not exist "%HITORI_OPEN_SCRIPT%" goto :missing_files
if not exist "%HITORI_GIT_SCRIPT%" goto :missing_files
if not exist "%HITORI_SETTINGS_FILE%" goto :missing_files

"%HITORI_PS%" -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -Command "$ErrorActionPreference='Stop'; Unblock-File -LiteralPath $env:HITORI_REGISTER_SCRIPT,$env:HITORI_OPEN_SCRIPT,$env:HITORI_GIT_SCRIPT"
if errorlevel 1 goto :unblock_failed

"%HITORI_PS%" -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File "%HITORI_REGISTER_SCRIPT%"
set "REGISTER_RESULT=%ERRORLEVEL%"
if not "%REGISTER_RESULT%"=="0" goto :registration_failed

echo Viewer registration finished.
pause
exit /b 0

:missing_powershell
echo ERROR: Windows PowerShell was not found.
echo Expected: "%HITORI_PS%"
pause
exit /b 1

:missing_files
echo ERROR: A required viewer file is missing.
echo Folder: "%~dp0"
echo Required: RegisterDumpViewer.ps1 OpenCrash.ps1 GitSource.ps1 HitoriCrash.vssettings
pause
exit /b 1

:unblock_failed
echo ERROR: Could not unblock the viewer scripts. See the PowerShell error above.
pause
exit /b 1

:registration_failed
echo ERROR: Registration returned exit code %REGISTER_RESULT%. See the error above.
pause
exit /b %REGISTER_RESULT%
