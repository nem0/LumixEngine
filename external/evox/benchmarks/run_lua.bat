@echo off
REM Evox Benchmark Suite - Lua reference ports
REM Uses ..\build\lua\lua54.exe (downloaded Lua 5.4 binaries), falling back
REM to lua on PATH.

echo.
echo === Lua Benchmark Suite ===
echo.

setlocal enabledelayedexpansion

set "BENCH_DIR=%~dp0."
set "LUA=%~dp0..\build\lua\lua54.exe"

if not exist "!LUA!" (
    where lua >nul 2>nul
    if errorlevel 1 (
        echo Error: lua54.exe not found in ..\build\lua and lua not in PATH
        exit /b 1
    )
    set "LUA=lua"
)

set "passed=0"
set "total=0"

for /f "delims=" %%A in ('dir /b /o:n "!BENCH_DIR!\*.lua"') do (
    set /a total+=1
    set "bench=%%A"

    echo Running: %%A
    "!LUA!" "!BENCH_DIR!\!bench!" 2>&1
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
