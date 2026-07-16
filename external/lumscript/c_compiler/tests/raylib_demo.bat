@echo off
setlocal

REM Emit and compile a LumScript raylib demo.
REM Set RAYLIB_DIR to override the downloaded raylib installation.
set "RAYLIB_VERSION=6.0"
set "RAYLIB_ARCH=win64_msvc16"
set "RAYLIB_ROOT=%~dp0\..\..\..\raylib"
set "RAYLIB_ARCHIVE=%RAYLIB_ROOT%\raylib-%RAYLIB_VERSION%_%RAYLIB_ARCH%.zip"
set "RAYLIB_AUTO_DOWNLOAD=0"
if not defined RAYLIB_DIR (
	set "RAYLIB_DIR=%~dp0\..\..\..\raylib\raylib-%RAYLIB_VERSION%_%RAYLIB_ARCH%"
	set "RAYLIB_AUTO_DOWNLOAD=1"
)

if not exist "%RAYLIB_DIR%\include\raylib.h" if "%RAYLIB_AUTO_DOWNLOAD%"=="1" (
	echo Downloading raylib %RAYLIB_VERSION% for Windows x64...
	powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; New-Item -ItemType Directory -Force -Path '%RAYLIB_ROOT%' | Out-Null; Invoke-WebRequest -UseBasicParsing 'https://github.com/raysan5/raylib/releases/download/%RAYLIB_VERSION%/raylib-%RAYLIB_VERSION%_%RAYLIB_ARCH%.zip' -OutFile '%RAYLIB_ARCHIVE%'; Expand-Archive -LiteralPath '%RAYLIB_ARCHIVE%' -DestinationPath '%RAYLIB_ROOT%' -Force; Remove-Item -LiteralPath '%RAYLIB_ARCHIVE%'"
	if errorlevel 1 (
		echo raylib download or extraction failed.
		exit /b 1
	)
)

if not defined RAYLIB_DIR (
	echo RAYLIB_DIR is not set.
	echo Example: set RAYLIB_DIR=C:\path\to\raylib
	exit /b 1
)

if not exist "%RAYLIB_DIR%\include\raylib.h" (
	echo raylib.h was not found under "%RAYLIB_DIR%\include".
	exit /b 1
)

if not exist "%RAYLIB_DIR%\lib\raylib.lib" (
	echo raylib.lib was not found under "%RAYLIB_DIR%\lib".
	exit /b 1
)

call "%~dp0/../../build.bat" lum_to_c
if errorlevel 1 exit /b 1

set OUT_DIR=%~dp0/../../build
set LUM_TO_C_EXE=%OUT_DIR%\lum_to_c.exe
set SOURCE=%~dp0raylib_demo.lum
set GENERATED_C=%~dp0raylib_demo.c
set GENERATED_EXE=%OUT_DIR%\raylib_demo.exe

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

cl.exe /nologo /TC /MD /I"%RAYLIB_DIR%\include" "%GENERATED_C%" /Fe"%GENERATED_EXE%" /link /LIBPATH:"%RAYLIB_DIR%\lib" raylib.lib opengl32.lib gdi32.lib winmm.lib user32.lib shell32.lib
if errorlevel 1 (
	echo raylib demo compilation failed.
	exit /b 1
)

copy /Y "%RAYLIB_DIR%\lib\raylib.dll" "%OUT_DIR%\raylib.dll" >nul
if errorlevel 1 (
	echo Could not copy raylib.dll.
	exit /b 1
)

"%GENERATED_EXE%"
exit /b %errorlevel%
