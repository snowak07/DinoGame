@echo off
REM ---------------------------------------------------------------------------
REM DinoGame: copy the last packaged build to the second test PC over the LAN.
REM
REM   deploy.bat          copy the package already in ARCHIVE to DEPLOY_TARGET
REM   deploy.bat check    only test that DEPLOY_TARGET is reachable
REM
REM This is the quick dev loop. itch (package.bat publish) is for playtests with
REM other people. package.bat deploy packages and then runs this.
REM
REM Close DinoGame.exe on the test PC first: a running game locks its exe and pak,
REM and those are the files a new build has to replace.
REM ---------------------------------------------------------------------------

setlocal

set SRC=H:\Unreal\Projects\Builds\Windows

REM The shared folder on the test PC. Set DEPLOY_TARGET in the environment to override
REM without editing this file. See docs/PIPELINE.md section 6 for setting up the share.
if not defined DEPLOY_TARGET set DEPLOY_TARGET=\\Sophie\DinoGame

REM dir rather than "if exist": "if exist" is unreliable on the root of a UNC share.
dir "%DEPLOY_TARGET%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Cannot reach %DEPLOY_TARGET%
    echo   Is the test PC on, and the folder shared? Open it in File Explorer once to
    echo   check, and to save the credentials Windows asks for.
    exit /b 1
)
if /I "%~1"=="check" exit /b 0

if not exist "%SRC%\DinoGame.exe" (
    echo ERROR: No package at %SRC%. Run package.bat first.
    exit /b 1
)

echo Deploying %SRC% to %DEPLOY_TARGET%
if exist "%~dp0Build\DinoBuildVersion.txt" (
    set /p DEPLOYVER=<"%~dp0Build\DinoBuildVersion.txt"
)
if defined DEPLOYVER echo Build version: %DEPLOYVER%

REM /MIR copies what changed and deletes what is gone, so files dropped from the build
REM do not linger on the test PC.
REM /XD Saved keeps both machines' Saved folders out of it: the test PC's own logs are
REM not purged, and this machine's logs are not copied over them. Excluded directories
REM are skipped by the purge as well as the copy.
REM /R:2 /W:2 fails within seconds on a locked file instead of robocopy's default of
REM retrying for days.
robocopy "%SRC%" "%DEPLOY_TARGET%" /MIR /XD Saved /MT:16 /R:2 /W:2 /NP /NDL

REM robocopy's exit code is a bitmask: 0-7 are all kinds of success (1 = files copied),
REM 8 and above means something failed to copy.
if errorlevel 8 (
    echo.
    echo *** DEPLOY FAILED - see the output above ***
    echo   A locked file usually means DinoGame.exe is still running on the test PC.
    exit /b 1
)

echo.
echo === DEPLOYED ===
echo Check the version on screen on the test PC matches the one above.
echo Its log: %DEPLOY_TARGET%\DinoGame\Saved\Logs\DinoGame.log
exit /b 0
