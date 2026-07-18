@echo off
REM LumScript Benchmark Suite - C reference ports (compiled with cl /O2)

echo.
echo === C Benchmark Suite ===
echo.

setlocal enabledelayedexpansion

set "BENCH_DIR=%~dp0."
set "OUT_DIR=%~dp0..\build\bench_c"
set VSDEV_CMD=

where cl.exe >nul 2>&1
if errorlevel 1 (
    for /f "usebackq delims=" %%I in (`where vswhere.exe 2^>nul`) do (
        for /f "usebackq delims=" %%J in (`"%%I" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDEV_CMD=%%J\Common7\Tools\VsDevCmd.bat"
    )
    if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Insiders\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Insiders\Common7\Tools\VsDevCmd.bat"
    if defined VSDEV_CMD (
        call "!VSDEV_CMD!" -no_logo -arch=x64 -host_arch=x64
        if errorlevel 1 exit /b !errorlevel!
    )
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo Error: cl.exe not found
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set "passed=0"
set "total=0"

for /f "delims=" %%A in ('dir /b /o:n "!BENCH_DIR!\*.c"') do (
    set /a total+=1
    set "bench=%%~nA"

    echo Building: %%A
    cl /nologo /O2 /W3 "!BENCH_DIR!\%%A" /Fe:"!OUT_DIR!\!bench!.exe" /Fo:"!OUT_DIR!\!bench!.obj" >nul
    if !errorlevel! neq 0 (
        echo   [FAIL] %%A failed to compile
        echo.
    ) else (
        echo Running: !bench!.exe
        "!OUT_DIR!\!bench!.exe" 2>&1
        if !errorlevel! equ 0 (
            echo   [OK] %%A completed successfully
            set /a passed+=1
        ) else (
            echo   [FAIL] %%A failed
        )
        echo.
    )
)

echo === Summary ===
echo Passed: !passed!/!total!
echo.

if !passed! equ !total! (
    exit /b 0
) else (
    exit /b 1
)
