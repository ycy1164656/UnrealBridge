@echo off
setlocal

set "SRC=%~dp0Plugin\UnrealBridge"
set "DST=%~1"
if "%DST%"=="" set "DST=C:\dev\ShooterRoyal_5_8_DirectUpgrade\Plugins\UnrealBridge"

echo Syncing UnrealBridge plugin...
echo   From: %SRC%
echo   To:   %DST%
echo.

rem Non-destructive synchronization: never remove destination-only files.
rem Binaries/Intermediate remain project-local build products.
robocopy "%SRC%" "%DST%" /E /XD Binaries Intermediate __pycache__ /XF *.pdb *.pyc 2>nul

if %ERRORLEVEL% LEQ 7 (
    echo.
    echo Done.
) else (
    echo.
    echo ERROR: robocopy failed with code %ERRORLEVEL%
)

endlocal
