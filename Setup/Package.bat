@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Package.bat [Release or Shipping] - default: Release
set "ERR="
set "STAGE="
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
if /i "%CONFIG%"=="Release" (set "CONFIG=Release") else if /i "%CONFIG%"=="Shipping" (set "CONFIG=Shipping") else goto :bad_config

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "PACKAGE_ROOT=%ROOT%\Build\Package"
set "OUTPUT=%PACKAGE_ROOT%\Hitori"
call :strlen ROOT ROOTLEN
call :strlen PACKAGE_ROOT PKGLEN
call :make_id
set "STAGE_NAME=.staging-Hitori-%ID%"
set "PREVIOUS_NAME=.previous-Hitori-%ID%"
set "STAGE=%PACKAGE_ROOT%\%STAGE_NAME%"
set "PREVIOUS=%PACKAGE_ROOT%\%PREVIOUS_NAME%"

for %%F in (Source Assets Resources GameConfig) do if not exist "%ROOT%\%%F\" (
    set "ERR=Required project folder is missing: %%F"
    goto :fail
)
for %%P in ("%OUTPUT%" "%STAGE%" "%PREVIOUS%") do (
    call :assert_package_path "%%~P"
    if errorlevel 1 goto :fail
)

set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" (
    set "ERR=Visual Studio Installer/vswhere.exe was not found."
    goto :fail
)
set "MSBUILD="
pushd "%VSWHERE_DIR%"
for /f "delims=" %%M in ('.\vswhere.exe -latest -prerelease -find MSBuild\**\Bin\MSBuild.exe 2^>nul') do if not defined MSBUILD set "MSBUILD=%%M"
popd
if not defined MSBUILD (
    set "ERR=MSBuild was not found. Install the Visual Studio C++ workload."
    goto :fail
)

set "SOLUTION=%ROOT%\Hitori.slnx"
set "GAME_PROJECT=%ROOT%\Build\ProjectFiles\HitoriGame.vcxproj"
set "HEADER_TOOL_PROJECT=%ROOT%\Build\ProjectFiles\HitoriHeaderTool.vcxproj"
set "NEED_GEN="
if not exist "%SOLUTION%" set "NEED_GEN=1"
if not exist "%GAME_PROJECT%" set "NEED_GEN=1"
if not exist "%HEADER_TOOL_PROJECT%" set "NEED_GEN=1"
if not defined NEED_GEN goto :build
"%~dp0premake5.exe" "--file=%~dp0premake5.lua" vs2026
if errorlevel 1 (
    set "ERR=Project generation failed."
    goto :fail
)

:build
set "VSLANG=1033"
rem Solution-only dependencies do not run for an individual vcxproj build.
rem Bootstrap the header tool explicitly, including the first Shipping build.
call :build_project "%HEADER_TOOL_PROJECT%"
if errorlevel 1 goto :fail
call :build_project "%GAME_PROJECT%"
if errorlevel 1 goto :fail

set "BINARY_ROOT=%ROOT%\Build\Bin\%CONFIG%-windows-x86_64"
for %%F in (HitoriGame.exe libfbxsdk.dll) do if not exist "%BINARY_ROOT%\%%F" (
    set "ERR=Required runtime file is missing after build: %%F"
    goto :fail
)
mkdir "%STAGE%" 2>nul
if not exist "%STAGE%\" (
    set "ERR=Could not create the staging directory."
    goto :fail
)
for %%F in (HitoriGame.exe libfbxsdk.dll) do (
    copy /y /b "%BINARY_ROOT%\%%F" "%STAGE%\%%F" >nul
    if errorlevel 1 (
        set "ERR=Could not copy %%F into the package."
        goto :fail
    )
)
for %%F in (Assets Resources GameConfig) do (
    call :copy_content %%F
    if errorlevel 1 goto :fail
)
for %%F in ("GameConfig\DefaultGame.ini" "Resources\Shader\DefaultShader.hlsl") do if not exist "%STAGE%\%%~F" (
    set "ERR=Required package content is missing: %%~F"
    goto :fail
)

rem Publish only after build and copy succeeded; restore the old output if publication fails.
if not exist "%OUTPUT%" goto :publish_stage
call :assert_package_path "%OUTPUT%"
if errorlevel 1 goto :fail
ren "%OUTPUT%" "%PREVIOUS_NAME%" 2>nul
if not exist "%OUTPUT%" goto :publish_stage
set "ERR=Could not move the previous package aside (is the game still running?): %OUTPUT%"
goto :fail

:publish_stage
call :assert_package_path "%STAGE%"
if errorlevel 1 goto :rollback
ren "%STAGE%" "Hitori" 2>nul
if exist "%OUTPUT%\HitoriGame.exe" goto :remove_previous
set "ERR=Could not publish the staged package to %OUTPUT%"

:rollback
set "PUBLISH_ERR=%ERR%"
if not exist "%PREVIOUS%\" goto :rollback_done
if exist "%OUTPUT%" goto :rollback_done
call :assert_package_path "%PREVIOUS%"
if errorlevel 1 goto :rollback_done
ren "%PREVIOUS%" "Hitori" 2>nul
:rollback_done
set "ERR=%PUBLISH_ERR%"
goto :fail

:remove_previous
if not exist "%PREVIOUS%" goto :done
call :assert_package_path "%PREVIOUS%"
if errorlevel 1 goto :previous_retained
rmdir /s /q "%PREVIOUS%" 2>nul
if not exist "%PREVIOUS%" goto :done
set "ERR=Could not remove %PREVIOUS%"
:previous_retained
echo WARNING: Package is ready; the previous output was retained: %ERR%

:done
call :cleanup_stage
echo [Package] Done: %OUTPUT%
exit /b 0

:bad_config
set "ERR=Configuration must be Release or Shipping: %CONFIG%"

:fail
echo [Package] Failed: %ERR%
call :cleanup_stage
exit /b 1

rem ---------------------------------------------------------------------------

:build_project
"%MSBUILD%" "%~1" -t:Build "-p:SolutionDir=%ROOT%/" "-p:Configuration=%CONFIG%" -p:Platform=x64 -m:1 -nr:false -nologo -v:minimal
set "BUILD_EXIT=%ERRORLEVEL%"
if "%BUILD_EXIT%"=="0" exit /b 0
set "ERR=Build failed for %~nx1 (exit %BUILD_EXIT%)."
exit /b 1

:copy_content
robocopy "%ROOT%\%~1" "%STAGE%\%~1" /E /R:1 /W:1 /NJH /NJS /NFL /NDL /XD ".fbx-test-fixtures*" ".fbx-import-*"
set "COPY_EXIT=%ERRORLEVEL%"
if %COPY_EXIT% LSS 8 exit /b 0
set "ERR=Content copy failed for %~1 (exit %COPY_EXIT%)."
exit /b 1

:cleanup_stage
if not defined STAGE exit /b 0
if not exist "%STAGE%" exit /b 0
call :assert_package_path "%STAGE%"
if errorlevel 1 goto :cleanup_stage_warn
rmdir /s /q "%STAGE%" 2>nul
if not exist "%STAGE%" exit /b 0
set "ERR=Could not remove %STAGE%"
:cleanup_stage_warn
echo WARNING: Temporary package directory was retained: %ERR%
exit /b 0

rem Sets ERR and fails when the path is outside Build\Package or passes through a link.
:assert_package_path
set "APP_PATH=%~f1"
call set "APP_PFX=%%APP_PATH:~0,%PKGLEN%%%"
call set "APP_SEP=%%APP_PATH:~%PKGLEN%,1%%"
if /i not "%APP_PFX%"=="%PACKAGE_ROOT%" goto :app_outside
if not "%APP_SEP%"=="\" goto :app_outside
call :check_chain "%APP_PATH%"
if not errorlevel 1 goto :app_children
set "ERR=Package path contains a filesystem link: %APP_PATH%"
exit /b 1
:app_children
if not exist "%APP_PATH%\" exit /b 0
dir /s /b /al "%APP_PATH%" >nul 2>&1
if errorlevel 1 exit /b 0
set "ERR=Package directory contains a filesystem link: %APP_PATH%"
exit /b 1
:app_outside
set "ERR=Package path is outside Build\Package: %APP_PATH%"
exit /b 1

rem Fails when the path is outside ROOT or any component below ROOT is a link.
:check_chain
setlocal
set "P=%~f1"
:check_chain_loop
if /i "%P%"=="%ROOT%" (endlocal & exit /b 0)
call set "PFX=%%P:~0,%ROOTLEN%%%"
call set "SEP=%%P:~%ROOTLEN%,1%%"
if /i not "%PFX%"=="%ROOT%" (endlocal & exit /b 1)
if not "%SEP%"=="\" (endlocal & exit /b 1)
set "ATTR="
if exist "%P%" for %%I in ("%P%") do set "ATTR=%%~aI"
if /i "%ATTR:~8,1%"=="l" (endlocal & exit /b 1)
for %%I in ("%P%\..") do set "P=%%~fI"
goto :check_chain_loop

:make_id
set "ID=%RANDOM%%RANDOM%%RANDOM%%RANDOM%"
if exist "%PACKAGE_ROOT%\.staging-Hitori-%ID%" goto :make_id
if exist "%PACKAGE_ROOT%\.previous-Hitori-%ID%" goto :make_id
exit /b 0

:strlen
setlocal
call set "S=%%%~1%%"
set "N=0"
:strlen_loop
if defined S (
    set "S=%S:~1%"
    set /a N+=1
    goto :strlen_loop
)
endlocal & set "%~2=%N%"
exit /b 0
