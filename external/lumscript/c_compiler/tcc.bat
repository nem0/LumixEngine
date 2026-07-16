@echo off
setlocal EnableDelayedExpansion

REM Build lum_to_c, cache the official TinyCC Windows binary locally, and
REM compile the generated C fixture without requiring a Visual Studio toolchain.
set SCRIPT_DIR=%~dp0
set TCC_DIR=%SCRIPT_DIR%tcc
set TCC_EXE=%TCC_DIR%\tcc\tcc.exe
set TCC_ZIP=%TCC_DIR%\tcc.zip
set TCC_URL=https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip
set OUT_DIR=%SCRIPT_DIR%..\build
set LUM_TO_C_EXE=%OUT_DIR%\lum_to_c.exe
set SOURCE=%SCRIPT_DIR%tests\lum_to_c.lum
set GENERATED_C=%SCRIPT_DIR%tests\lum_to_c.c
set GENERATED_EXE=%OUT_DIR%\lum_to_c.tcc.exe

if not exist "%TCC_EXE%" (
	if not exist "%TCC_DIR%" mkdir "%TCC_DIR%"
	echo Downloading TinyCC...
	powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing '%TCC_URL%' -OutFile '%TCC_ZIP%'"
	if errorlevel 1 (
		echo TinyCC download failed.
		exit /b 1
	)
	powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%TCC_ZIP%' -DestinationPath '%TCC_DIR%' -Force; Remove-Item -LiteralPath '%TCC_ZIP%'"
	if errorlevel 1 (
		echo TinyCC extraction failed.
		exit /b 1
	)
)

if not exist "%TCC_EXE%" (
	echo tcc.exe was not found after extraction.
	exit /b 1
)

call "%SCRIPT_DIR%..\build.bat" lum_to_c
if errorlevel 1 exit /b 1

"%LUM_TO_C_EXE%" "%SOURCE%" "%GENERATED_C%"
if errorlevel 1 (
	echo C emission failed.
	exit /b 1
)

"%TCC_EXE%" "%GENERATED_C%" -luser32 -o "%GENERATED_EXE%"
if errorlevel 1 (
	echo Generated C failed to compile with TinyCC.
	exit /b 1
)

"%GENERATED_EXE%"
if errorlevel 1 (
	echo Generated executable returned %errorlevel%.
	exit /b 1
)

echo TinyCC C conversion test passed.
exit /b 0
