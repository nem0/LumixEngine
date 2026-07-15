@echo off
REM Build lumc.exe and run the lumc smoke test

call "%~dp0/../build.bat"
if %ERRORLEVEL% NEQ 0 (
	echo lumc build failed.
	exit /b 1
)

set OUT_DIR=%~dp0/../build
set LUMC_EXE=%OUT_DIR%\lumc.exe
set SMOKE_SCRIPT=%~dp0lumc_test.lum
set SMOKE_OUTPUT=%TEMP%\lumc_smoke_%RANDOM%.out

if not exist "%LUMC_EXE%" (
	echo lumc.exe not found!
	exit /b 1
)
if not exist "%SMOKE_SCRIPT%" (
	echo lumc_test.lum not found!
	exit /b 1
)

echo Running lumc smoke test...
"%LUMC_EXE%" "%SMOKE_SCRIPT%" > "%SMOKE_OUTPUT%" 2>&1
if %ERRORLEVEL% NEQ 0 (
	echo lumc smoke test failed.
	del /q /f "%SMOKE_OUTPUT%" >nul 2>&1
	exit /b 1
)
findstr /x /c:"lumc smoke test" "%SMOKE_OUTPUT%" >nul
if %ERRORLEVEL% NEQ 0 (
	echo lumc smoke test produced unexpected output:
	type "%SMOKE_OUTPUT%"
	del /q /f "%SMOKE_OUTPUT%" >nul 2>&1
	exit /b 1
)
del /q /f "%SMOKE_OUTPUT%" >nul 2>&1

echo lumc smoke test passed.
