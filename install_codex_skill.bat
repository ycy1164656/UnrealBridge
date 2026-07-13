@echo off
setlocal

rem Install the Codex-flavoured UnrealBridge skill into the current user's
rem Codex skill directory. Re-runnable: existing target is replaced.

set "SRC=%~dp0.codex\skills\unreal-bridge"
set "DST=%USERPROFILE%\.codex\skills\unreal-bridge"

if not exist "%SRC%" (
    echo ERROR: %SRC% does not exist.
    exit /b 1
)

if exist "%DST%" (
    rmdir /S /Q "%DST%"
)

mkdir "%USERPROFILE%\.codex\skills" 2>nul
xcopy "%SRC%" "%DST%\" /E /I /Y >nul
if errorlevel 1 (
    echo ERROR: failed to copy Codex skill.
    exit /b 1
)

echo Codex skill installed:
echo   %DST%

endlocal
