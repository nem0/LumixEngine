@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 ^<file.evox^>
    exit /b 2
)

set "ROOT_DIR=%~dp0.."
set "EVOXC=%ROOT_DIR%\external\evox\build\evoxc.exe"
set "SOURCE=%~f1"

if not exist "%EVOXC%" (
    echo Error: evoxc.exe not found at "%EVOXC%"
    echo Build it with external\evox\build.bat
    exit /b 1
)
if not exist "%SOURCE%" (
    echo Error: source file not found: "%SOURCE%"
    exit /b 1
)

"%EVOXC%" --typecheck-only --import-dir "%ROOT_DIR%\demo" --core-dir "%ROOT_DIR%\data\scripts" "%SOURCE%"
exit /b %errorlevel%
