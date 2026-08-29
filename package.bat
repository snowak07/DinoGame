@echo off
REM ---------------------------------------------------------------------------
REM DinoGame: build + cook + package, no editor UI required.
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
set PROJ=H:\Unreal\Projects\DinoGame\DinoGame.uproject
set ARCHIVE=H:\Unreal\Projects\Builds

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

echo.
echo === [1/2] Editor target (needed for a correct cook) ===
call "%UE%\Engine\Build\BatchFiles\Build.bat" DinoGameEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 goto :fail

echo.
echo === [2/2] Game target + cook + stage + package ===
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%PROJ%" ^
    -noP4 ^
    -platform=Win64 ^
    -clientconfig=Development ^
    -build -cook -stage -pak -archive ^
    -archivedirectory="%ARCHIVE%"
if errorlevel 1 goto :fail

echo.
echo === DONE ===
echo Package: %ARCHIVE%\Windows
echo Run it with:  "%ARCHIVE%\Windows\DinoGame.exe" -log
exit /b 0

:fail
echo.
echo *** FAILED - see the output above ***
exit /b 1
