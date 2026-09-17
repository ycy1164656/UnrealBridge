@echo off
setlocal

rem Install the UnrealBridge runtime plus the Codex-specific SKILL.md into the
rem current user's Codex skill directory. Re-runnable and non-destructive.

set "RUNTIME_SRC=%~dp0.claude\skills\unreal-bridge"
set "CODEX_SRC=%~dp0.codex\skills\unreal-bridge"
set "DST=%USERPROFILE%\.codex\skills\unreal-bridge"

if not exist "%RUNTIME_SRC%" (
	echo ERROR: %RUNTIME_SRC% does not exist.
	exit /b 1
)
if not exist "%CODEX_SRC%" (
	echo ERROR: %CODEX_SRC% does not exist.
	exit /b 1
)

mkdir "%USERPROFILE%\.codex\skills" 2>nul
robocopy "%RUNTIME_SRC%" "%DST%" /E /XD __pycache__ /XF *.pyc >nul
if errorlevel 8 (
	echo ERROR: failed to copy UnrealBridge runtime into Codex skill.
	exit /b 1
)
robocopy "%CODEX_SRC%" "%DST%" /E /XD __pycache__ /XF *.pyc >nul
if errorlevel 8 (
	echo ERROR: failed to overlay Codex-specific skill files.
	exit /b 1
)

echo Codex skill installed:
echo   %DST%

endlocal
