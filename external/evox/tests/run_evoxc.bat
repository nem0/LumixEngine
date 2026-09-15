@echo off
REM Build evoxc.exe and run the evoxc smoke test

call "%~dp0/../build.bat"
if %ERRORLEVEL% NEQ 0 (
	echo evoxc build failed.
	exit /b 1
)

set OUT_DIR=%~dp0/../build
set EVOXC_EXE=%OUT_DIR%\evoxc.exe
set SMOKE_SCRIPT=%~dp0evoxc_test.evox
set SMOKE_OUTPUT=%TEMP%\evoxc_smoke_%RANDOM%.out

if not exist "%EVOXC_EXE%" (
	echo evoxc.exe not found!
	exit /b 1
)
if not exist "%SMOKE_SCRIPT%" (
	echo evoxc_test.evox not found!
	exit /b 1
)

echo Running evoxc smoke test...
"%EVOXC_EXE%" "%SMOKE_SCRIPT%" > "%SMOKE_OUTPUT%" 2>&1
if %ERRORLEVEL% NEQ 0 (
	echo evoxc smoke test failed.
	del /q /f "%SMOKE_OUTPUT%" >nul 2>&1
	exit /b 1
)
findstr /x /c:"evoxc smoke test" "%SMOKE_OUTPUT%" >nul
if %ERRORLEVEL% NEQ 0 (
	echo evoxc smoke test produced unexpected output:
	type "%SMOKE_OUTPUT%"
	del /q /f "%SMOKE_OUTPUT%" >nul 2>&1
	exit /b 1
)
del /q /f "%SMOKE_OUTPUT%" >nul 2>&1

echo evoxc smoke test passed.
