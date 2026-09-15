@echo off
REM ---------------------------------------------------------------------------
REM DinoGame: build the EDITOR target only.
REM
REM Run this after any C++ change, BEFORE opening the editor.
REM
REM Why this script exists, and why the game-target build in CLAUDE.md is not a
REM substitute: the editor loads Binaries/Win64/UnrealEditor-DinoGame.dll, which
REM only the DinoGameEditor target produces. Building DinoGame (the game target)
REM compiles the same source into DinoGame.exe and never touches that DLL, so a
REM clean game-target build tells you nothing about whether the editor can start.
REM
REM When a DinoGameEditor build fails, UBT has already deleted the old DLL - it
REM removes outdated outputs before running the compile actions, not after they
REM succeed. So a failed compile leaves no editor module at all, and the next
REM launch shows "The following modules are missing or built with a different
REM engine version". Answering Yes there re-runs this same failing compile and
REM reports only "DinoGame could not be compiled" with none of the errors.
REM
REM Running the build here instead prints the actual compiler diagnostics.
REM ---------------------------------------------------------------------------

setlocal

set UE=H:\Unreal\UE_5.8
set PROJ=H:\Unreal\Projects\DinoGame\DinoGame.uproject

REM The editor holds UnrealEditor-DinoGame.dll open, so the link step cannot
REM overwrite it while the editor is running. Live Coding blocks this target too.
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
    echo ERROR: The Unreal editor is running. Close it first.
    exit /b 1
)

call "%UE%\Engine\Build\BatchFiles\Build.bat" DinoGameEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 (
    echo.
    echo *** EDITOR BUILD FAILED - fix the errors above before opening the editor. ***
    echo     The editor module is gone until this succeeds; launching now will
    echo     prompt about missing modules.
    exit /b 1
)

echo.
echo === Editor module built. Safe to open the editor. ===
exit /b 0
