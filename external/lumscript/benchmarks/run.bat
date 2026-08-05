@echo off
REM LumScript Benchmark Suite Runner

echo.
echo === LumScript Benchmark Suite ===
echo.

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "LUMC=%ROOT%\build\lumc.exe"
set "BENCH_DIR=%~dp0."

echo Building optimized lumc.exe...
pushd "%ROOT%"
call "%ROOT%\build.bat" release
set "BUILD_RESULT=!errorlevel!"
popd
if not "!BUILD_RESULT!"=="0" (
    echo Error: Failed to build lumc.exe
    exit /b 1
)
if not exist "!LUMC!" (
    echo Error: Failed to build lumc.exe
    exit /b 1
)
echo.

set "passed=0"
set "total=0"

for /f "delims=" %%A in ('dir /b /o:n "!BENCH_DIR!\*.ls"') do (
	set "bench=%%A"

	echo Running: %%A [bytecode]
	"!LUMC!" "!BENCH_DIR!\!bench!" 2>&1
	if !errorlevel! equ 0 (
		echo   [OK] %%A bytecode completed successfully
		set /a passed+=1
	) else (
		echo   [FAIL] %%A bytecode failed
	)
	set /a total+=1

	echo Running: %%A [MIR]
	"!LUMC!" --mir "!BENCH_DIR!\!bench!" 2>&1
	if !errorlevel! equ 0 (
		echo   [OK] %%A MIR completed successfully
		set /a passed+=1
	) else (
		echo   [FAIL] %%A MIR failed
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
