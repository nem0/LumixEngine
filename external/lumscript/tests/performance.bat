@echo off
setlocal enabledelayedexpansion

REM Build and run performance test (fib(30))

set SCRIPT_DIR=%~dp0
set EXT_DIR=%SCRIPT_DIR%..
set OUT_DIR=%EXT_DIR%\build
set COMPILER=cl

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

REM Find compiler
where cl.exe >nul 2>&1
if errorlevel 1 (
	if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
		call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
	)
)

REM Compile performance.cpp with other source files
set COMMON_FLAGS=/nologo /EHsc /Zi /I"%EXT_DIR%"
set CXXFLAGS=%COMMON_FLAGS% /std:c++20 /D_CRT_NONSTDC_NO_DEPRECATE /DSTATIC_PLUGINS /O2 /MD
set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\performance.exe"

echo Compiling performance test...
%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%performance.cpp" "%EXT_DIR%\parser.cpp" "%EXT_DIR%\compiler.cpp" "%EXT_DIR%\bytecode_compiler.cpp" "%EXT_DIR%\capi.cpp"
if errorlevel 1 (
	echo Compilation failed.
	exit /b 1
)

%COMPILER% %CXXFLAGS% /c "%EXT_DIR%\runtime.c"
if errorlevel 1 (
	echo Compilation failed.
	exit /b 1
)

%COMPILER% "performance.obj" "parser.obj" "compiler.obj" "bytecode_compiler.obj" "runtime.obj" "capi.obj" /link %LDFLAGS%
if errorlevel 1 (
	echo Linking failed.
	exit /b 1
)

if not exist "%OUT_DIR%\performance.exe" (
	echo performance.exe not found!
	exit /b 1
)

echo Running fib(30) performance test...
"%OUT_DIR%\performance.exe"
