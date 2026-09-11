@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\rebuild.ps1" -Mode Deploy
if errorlevel 1 (
    echo [ERROR] Build or deployment failed. See the message above.
    if not defined MEDIAPLAYER_NO_PAUSE pause
    exit /b 1
)
exit /b 0
