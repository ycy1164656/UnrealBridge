@echo off
setlocal
rem Preview by default. Use --apply after reviewing the exact router diff.
rem Never copy a second runtime or overwrite customized instructions silently.
python "%~dp0tools\install_codex_skill.py" %*
exit /b %ERRORLEVEL%
