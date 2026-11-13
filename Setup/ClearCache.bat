@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem ClearCache.bat [preview] [nopause]
rem Deletes generated build/import caches. Source assets, settings and SDKs stay intact.
set "PREVIEW="
set "NOPAUSE="
for %%A in (%*) do (
    if /i "%%~A"=="preview" set "PREVIEW=1"
    if /i "%%~A"=="nopause" set "NOPAUSE=1"
)
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "RESULT=0"
call :strlen ROOT ROOTLEN

rem Every build byproduct lives under Build\. Generated project files (Build\ProjectFiles) survive cleanup.
if not exist "%ROOT%\Build\" goto :scan_legacy
call :check_chain "%ROOT%\Build"
if errorlevel 1 (
    echo [refused] Build is a filesystem link.
    set "RESULT=1"
    goto :finish
)
for /f "delims=" %%E in ('dir /b /a "%ROOT%\Build" 2^>nul') do (
    call :remove_build_child "%%E"
    if errorlevel 1 goto :finish
)

:scan_legacy
rem Older layouts kept generated headers and shader caches in a root Intermediate folder.
if not exist "%ROOT%\Intermediate\" goto :scan_content
call :remove_target "%ROOT%\Intermediate"
if errorlevel 1 goto :finish

:scan_content
rem .fbxbin / .hasset are persistent assets, not caches. Only known bake
rem suffixes with their expected magic may be removed.
for %%C in (Assets Resources) do if exist "%ROOT%\%%C\" (
    for /f "delims=" %%F in ('dir /s /b /a-d "%ROOT%\%%C\*.bin" 2^>nul') do (
        call :check_bake "%%F"
        if errorlevel 1 goto :finish
    )
)

:finish
if not "%RESULT%"=="0" goto :end
echo Done. Assets, Resources, GameConfig, Settings, Logs, Saved, project files and installed SDKs are preserved.
if not defined PREVIEW echo Rebuild the solution before launching the editor.
goto :end

:end
if defined CI exit /b %RESULT%
if defined NOPAUSE exit /b %RESULT%
pause
exit /b %RESULT%

rem ---------------------------------------------------------------------------

:remove_build_child
if /i "%~1"=="ProjectFiles" exit /b 0
call :remove_target "%ROOT%\Build\%~1"
exit /b

:check_bake
if /i not "%~x1"==".bin" exit /b 0
set "MAGIC="
for %%N in ("%~n1") do (
    if /i "%%~xN"==".obj" set "MAGIC=SMBK"
    if /i "%%~xN"==".fbx" set "MAGIC=SKBK"
    if /i "%%~xN"==".anim" for %%M in ("%%~nN") do if /i "%%~xM"==".fbx" set "MAGIC=ANBK"
)
if not defined MAGIC exit /b 0
set "HEADER="
set /p "HEADER=" 0<"%~1"
setlocal EnableDelayedExpansion
set "MATCH="
if "!HEADER:~0,4!"=="!MAGIC!" set "MATCH=1"
endlocal & set "MATCH=%MATCH%"
if not defined MATCH exit /b 0
call :remove_target "%~1"
exit /b

:remove_target
set "TARGET=%~f1"
call set "LABEL=%%TARGET:~%ROOTLEN%%%"
set "LABEL=%LABEL:~1%"
call :check_chain "%TARGET%"
if not errorlevel 1 goto :remove_target_inner
call :print "[refused]" " - outside the project or behind a filesystem link."
set "RESULT=1"
exit /b 1

:remove_target_inner
set "ISDIR="
for %%I in ("%TARGET%") do set "ATTR=%%~aI"
if /i "%ATTR:~0,1%"=="d" set "ISDIR=1"
if not defined ISDIR goto :remove_target_go
dir /s /b /al "%TARGET%" >nul 2>&1
if errorlevel 1 goto :remove_target_go
call :print "[refused]" " - directory contains a filesystem link."
set "RESULT=1"
exit /b 1

:remove_target_go
if not defined PREVIEW goto :remove_target_delete
call :print "[preview]" ""
exit /b 0

:remove_target_delete
if defined ISDIR (rmdir /s /q "%TARGET%" 2>nul) else (del /f /q /a "%TARGET%" 2>nul)
if not exist "%TARGET%" goto :remove_target_done
call :print "[failed]" " - could not be removed."
set "RESULT=1"
exit /b 1

:remove_target_done
call :print "[removed]" ""
exit /b 0

rem Prints "<prefix> <LABEL><suffix>" safely for any characters in LABEL.
:print
setlocal EnableDelayedExpansion
echo(%~1 !LABEL!%~2
endlocal
exit /b 0

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
