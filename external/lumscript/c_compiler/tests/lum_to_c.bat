@echo off
setlocal

REM Build lum_to_c, emit C from the scalar backend fixture, then compile and run it.
call "%~dp0/../../build.bat" lum_to_c
if errorlevel 1 (
	echo lum_to_c build failed.
	exit /b 1
)

set OUT_DIR=%~dp0/../../build
set LUM_TO_C_EXE=%OUT_DIR%\lum_to_c.exe
set SOURCE=%~dp0lum_to_c.lum
set GENERATED_C=%~dp0lum_to_c.c
set GENERATED_EXE=%OUT_DIR%\lum_to_c.generated.exe

if not exist "%LUM_TO_C_EXE%" (
	echo lum_to_c.exe not found.
	exit /b 1
)

"%LUM_TO_C_EXE%" "%SOURCE%" "%GENERATED_C%"
if errorlevel 1 (
	echo C emission failed.
	exit /b 1
)

where cl.exe >nul 2>&1
if errorlevel 1 (
	echo cl.exe is not available.
	exit /b 1
)

cl.exe /nologo /TC "%GENERATED_C%" /Fe"%GENERATED_EXE%" /link user32.lib
if errorlevel 1 (
	echo Generated C failed to compile.
	exit /b 1
)

"%GENERATED_EXE%"
if errorlevel 1 (
	echo Generated executable returned %errorlevel%.
	exit /b 1
)

echo lum_to_c test passed.
exit /b 0
