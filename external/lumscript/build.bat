@echo off
setlocal enabledelayedexpansion

REM build.bat - Build lumc.exe standalone LumScript runner using cl.exe or clang-cl
REM
REM Usage:
REM   build.bat
REM   build.bat tests
REM   build.bat clang
REM   build.bat tests clang

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%..\..\src
set EXT_DIR=%SCRIPT_DIR%.
set OUT_DIR=%SCRIPT_DIR%build
set TARGET=lumc
set COMPILER=cl
set USE_CLANG=0

if /I "%~1"=="tests" set TARGET=tests
if /I "%~2"=="tests" set TARGET=tests

if /I "%~1"=="clang" set COMPILER=clang-cl
if /I "%~1"=="clang-cl" set COMPILER=clang-cl
if /I "%~1"=="/clang" set COMPILER=clang-cl
if /I "%~2"=="clang" set COMPILER=clang-cl
if /I "%~2"=="clang-cl" set COMPILER=clang-cl
if /I "%~2"=="/clang" set COMPILER=clang-cl

if /I "%COMPILER%"=="clang-cl" set USE_CLANG=1

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

if "%USE_CLANG%"=="0" (
    REM Verify x64 cl.exe is available
    cl.exe 2>&1 | findstr /C:"for x64" >nul 2>&1
    if errorlevel 1 (
        echo Warning: cl.exe is not targeting x64.
    )
) else (
    where clang-cl >nul 2>&1
    if errorlevel 1 (
        echo Error: clang-cl is not available in PATH.
        exit /b 1
    )
)

set COMMON_FLAGS=/nologo /EHsc /Zi /I"%SRC_DIR%" /I"%EXT_DIR%"
set CFLAGS=%COMMON_FLAGS% /D_CRT_NONSTDC_NO_DEPRECATE /DSTATIC_PLUGINS
set CXXFLAGS=%COMMON_FLAGS% /std:c++20 /D_CRT_NONSTDC_NO_DEPRECATE /DSTATIC_PLUGINS
if /I "%TARGET%"=="tests" (
    set CFLAGS=%CFLAGS% /Od /MDd
    set CXXFLAGS=%CXXFLAGS% /Od /MDd
) else (
    if /I "%~2"=="release" (
        set CFLAGS=%CFLAGS% /O2 /MD
        set CXXFLAGS=%CXXFLAGS% /O2 /MD
    ) else (
        set CFLAGS=%CFLAGS% /Od /MDd
        set CXXFLAGS=%CXXFLAGS% /Od /MDd
    )
)

if /I "%TARGET%"=="tests" (
    set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\tests.exe"
) else (
    set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\lumc.exe"
)

REM Clean up stale objects from previous runs.
del /q /f "%SCRIPT_DIR%*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\*.obj" >nul 2>&1

if /I "%TARGET%"=="tests" (
	%COMPILER% %CXXFLAGS% "%SCRIPT_DIR%tests/main.cpp" "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%runtime.cpp" "%SCRIPT_DIR%capi.cpp" /link %LDFLAGS%
) else (
	if /I "%USE_CLANG%"=="1" (
		%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%lumc.c"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%runtime.cpp" "%SCRIPT_DIR%capi.cpp"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% "%SCRIPT_DIR%lumc.obj" "%SCRIPT_DIR%parser.obj" "%SCRIPT_DIR%compiler.obj" "%SCRIPT_DIR%bytecode_compiler.obj" "%SCRIPT_DIR%runtime.obj" "%SCRIPT_DIR%capi.obj" /link %LDFLAGS%
	) else (
		%COMPILER% %CXXFLAGS%  "%SCRIPT_DIR%lumc.c" "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%runtime.cpp" "%SCRIPT_DIR%capi.cpp" /link %LDFLAGS%
	)
)

set RESULT=%errorlevel%

REM Clean up intermediate files created by cl/cland-cl.
del /q /f "%SCRIPT_DIR%*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%vc*.pdb" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\vc*.pdb" >nul 2>&1

exit /b %RESULT%
