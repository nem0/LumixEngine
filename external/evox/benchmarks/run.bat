@echo off
REM Evox Benchmark Suite Runner

echo.
echo === Evox Benchmark Suite ===
echo.

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "EVOXC=%ROOT%\build\evoxc.exe"
set "BENCH_DIR=%~dp0."

echo Building optimized evoxc.exe...
pushd "%ROOT%"
call "%ROOT%\build.bat" release
set "BUILD_RESULT=!errorlevel!"
popd
if not "!BUILD_RESULT!"=="0" (
    echo Error: Failed to build evoxc.exe
    exit /b 1
)
if not exist "!EVOXC!" (
    echo Error: Failed to build evoxc.exe
    exit /b 1
)
echo.

set "passed=0"
set "total=0"

for /f "delims=" %%A in ('dir /b /o:n "!BENCH_DIR!\*.evox"') do (
	set "bench=%%A"

	echo Running: %%A [bytecode]
	"!EVOXC!" "!BENCH_DIR!\!bench!" 2>&1
	if !errorlevel! equ 0 (
		echo   [OK] %%A bytecode completed successfully
		set /a passed+=1
	) else (
		echo   [FAIL] %%A bytecode failed
	)
	set /a total+=1

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
