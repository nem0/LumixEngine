@echo off
REM Evox Benchmark Suite - Node.js reference ports, JIT disabled
REM Runs the same *.js benchmarks as run_js.bat with V8's Ignition
REM interpreter only (node --jitless), for a fairer comparison against
REM the evoxc bytecode interpreter.

echo.
echo === Node.js Benchmark Suite (jitless) ===
echo.

setlocal enabledelayedexpansion

set "BENCH_DIR=%~dp0."

where node >nul 2>nul
if errorlevel 1 (
    echo Error: node not found in PATH
    exit /b 1
)

set "passed=0"
set "total=0"

for /f "delims=" %%A in ('dir /b /o:n "!BENCH_DIR!\*.js"') do (
    set /a total+=1
    set "bench=%%A"

    echo Running: %%A
    node --jitless "!BENCH_DIR!\!bench!" 2>&1
    if !errorlevel! equ 0 (
        echo   [OK] %%A completed successfully
        set /a passed+=1
    ) else (
        echo   [FAIL] %%A failed
    )
    echo.
)

echo === Summary ===
echo Passed: !passed!/!total!
echo.

if !passed! equ !total! (
    exit /b 0
) else (
    exit /b 1
)
