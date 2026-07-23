@echo off
REM Build tests.exe and run it

set BUILD_OUTPUT=%TEMP%\lumscript_build_%RANDOM%.log
call "%~dp0/../build.bat" tests > "%BUILD_OUTPUT%" 2>&1
if errorlevel 1 (
	echo Build failed.
	type "%BUILD_OUTPUT%"
	del /q /f "%BUILD_OUTPUT%" >nul 2>&1
	exit /b 1
)
del /q /f "%BUILD_OUTPUT%" >nul 2>&1

set OUT_DIR=%~dp0/../build
set TEST_EXE=%OUT_DIR%\tests.exe

if not exist "%TEST_EXE%" (
	echo tests.exe not found!
	exit /b 1
)

echo Running tests.exe...
"%TEST_EXE%" %*
