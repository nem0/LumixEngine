@echo off
REM Dump Evox bytecode for all benchmarks

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "EVOXC=%ROOT%\build\evoxc.exe"
set "BENCH_DIR=%~dp0."
set "OUT_DIR=%BENCH_DIR%\bytecode"

if not exist "!EVOXC!" (
    echo Error: evoxc.exe not found: !EVOXC!
    echo Build the project first with build.bat release.
    exit /b 1
)

if not exist "!OUT_DIR!" mkdir "!OUT_DIR!"

set "failed=0"
for %%F in ("!BENCH_DIR!\*.evox") do (
    echo Dumping %%~nxF...
    "!EVOXC!" --dump-bytecode "%%~fF" > "!OUT_DIR!\%%~nF.dump"
    if errorlevel 1 (
        echo   [FAIL] %%~nxF
        set "failed=1"
    ) else (
        echo   [OK]   !OUT_DIR!\%%~nF.dump
    )
)

if "!failed!"=="1" exit /b 1
exit /b 0
