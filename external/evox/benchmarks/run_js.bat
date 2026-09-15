@echo off
REM Evox Benchmark Suite - Node.js reference ports

echo.
echo === Node.js Benchmark Suite ===
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
    node "!BENCH_DIR!\!bench!" 2>&1
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
