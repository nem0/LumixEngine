@echo off
REM Dump LumScript bytecode for all benchmarks

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "LUMC=%ROOT%\build\lumc.exe"
set "BENCH_DIR=%~dp0."
set "OUT_DIR=%BENCH_DIR%\bytecode"

if not exist "!LUMC!" (
    echo Error: lumc.exe not found: !LUMC!
    echo Build the project first with build.bat release.
    exit /b 1
)

if not exist "!OUT_DIR!" mkdir "!OUT_DIR!"

set "failed=0"
for %%F in ("!BENCH_DIR!\*.ls") do (
    echo Dumping %%~nxF...
    "!LUMC!" --dump-bytecode "%%~fF" > "!OUT_DIR!\%%~nF.dump"
    if errorlevel 1 (
        echo   [FAIL] %%~nxF
        set "failed=1"
    ) else (
        echo   [OK]   !OUT_DIR!\%%~nF.dump
    )
)

if "!failed!"=="1" exit /b 1
exit /b 0
