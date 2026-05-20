@echo off
REM Build tests.exe and run it

call "%~dp0/../build.bat" tests
if %ERRORLEVEL% NEQ 0 (
	echo Build failed.
	exit /b 1
)

set OUT_DIR=%~dp0/../build
set TEST_EXE=%OUT_DIR%\tests.exe

if not exist "%TEST_EXE%" (
	echo tests.exe not found!
	exit /b 1
)

echo Running tests.exe...
"%TEST_EXE%"
