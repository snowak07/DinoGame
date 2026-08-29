@echo off
REM ---------------------------------------------------------------------------
REM DinoGame: build + cook + package, and optionally publish to itch.io.
REM
REM   package.bat            build and package only
REM   package.bat publish    build, package, and upload to itch
REM
REM Close the Unreal editor and every running DinoGame.exe before running this.
REM Both lock files this needs to overwrite.
REM
REM The editor target is built FIRST and separately, on purpose: cooking runs
REM through UnrealEditor-DinoGame.dll, so a stale editor module cooks Blueprints
REM against an out-of-date C++ layout. That mismatch does not fail the build - it
REM produces a package that crashes on load with "Bad export index".
REM ---------------------------------------------------------------------------

setlocal

set UE=H:\Unreal\UE_5.8
set PROJDIR=H:\Unreal\Projects\DinoGame
set PROJ=%PROJDIR%\DinoGame.uproject
set ARCHIVE=H:\Unreal\Projects\Builds

REM butler wants "user/game", not the page URL. https://snowak3.itch.io/dinogame
REM means user "snowak3" and game slug "dinogame". Combined with the channel below,
REM butler pushes to snowak3/dinogame:windows-playtest.
set ITCH_TARGET=snowak3/dinogame
set ITCH_CHANNEL=windows-playtest

set PUBLISH=0
if /I "%~1"=="publish" set PUBLISH=1

tasklist /FI "IMAGENAME eq DinoGame.exe" 2>nul | find /I "DinoGame.exe" >nul
if not errorlevel 1 (
    echo ERROR: DinoGame.exe is running. Close it first.
    exit /b 1
)
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
    echo ERROR: The Unreal editor is running. Close it first.
    exit /b 1
)

REM Fail before a 10-minute build rather than after it.
if "%PUBLISH%"=="1" (
    if "%ITCH_TARGET%"=="" (
        echo ERROR: ITCH_TARGET is not set. Edit this script and set it to ^<itch-user^>/^<project^>.
        exit /b 1
    )
    where butler >nul 2>&1
    if errorlevel 1 (
        echo ERROR: butler is not on PATH.
        echo   If you just installed it, this shell is holding a stale environment -
        echo   open a new terminal and run again. A child process inherits the PATH of
        echo   whatever launched it, so an old shell passes the old PATH down to this
        echo   script. Otherwise see docs/PIPELINE.md section 6.
        exit /b 1
    )
)

echo.
echo === [1/3] Editor target (needed for a correct cook) ===
call "%UE%\Engine\Build\BatchFiles\Build.bat" DinoGameEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 goto :fail

echo.
echo === [2/3] Game target + cook + stage + package ===
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%PROJ%" ^
    -noP4 ^
    -platform=Win64 ^
    -clientconfig=Development ^
    -build -cook -stage -pak -compressed -archive ^
    -archivedirectory="%ARCHIVE%" ^
    -nocompileeditor -utf8output
if errorlevel 1 goto :fail

if "%PUBLISH%"=="0" (
    echo.
    echo === DONE ^(not published^) ===
    echo Package: %ARCHIVE%\Windows
    echo Run it with:  "%ARCHIVE%\Windows\DinoGame.exe" -log
    echo Upload it with:  package.bat publish
    exit /b 0
)

REM Stamp the upload with the commit it came from, so a tester saying "build a3f91c2
REM is broken" maps to an exact tree. A build made with uncommitted changes is marked
REM -dirty, because that SHA does not describe what is actually in the package.
set GITVER=unknown
for /f "delims=" %%i in ('git -C "%PROJDIR%" rev-parse --short HEAD 2^>nul') do set GITVER=%%i
git -C "%PROJDIR%" diff --quiet HEAD 2>nul
if errorlevel 1 set GITVER=%GITVER%-dirty

echo.
echo === [3/3] Publishing to itch as %ITCH_TARGET%:%ITCH_CHANNEL% (version %GITVER%) ===
butler push "%ARCHIVE%\Windows" %ITCH_TARGET%:%ITCH_CHANNEL% --userversion %GITVER%
if errorlevel 1 goto :fail

echo.
echo === DONE ===
echo Published %GITVER% to %ITCH_TARGET%:%ITCH_CHANNEL%
echo Testers on the itch app get it as a patch automatically.
exit /b 0

:fail
echo.
echo *** FAILED - see the output above ***
exit /b 1
