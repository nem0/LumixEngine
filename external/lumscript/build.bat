@echo off
setlocal enabledelayedexpansion

REM build.bat - Build lumc.exe standalone LumScript runner using cl.exe or clang-cl
REM
REM Usage:
REM   build.bat
REM   build.bat tests
REM   build.bat lum_to_c
REM   build.bat clang
REM   build.bat tests clang

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%..\..\src
set EXT_DIR=%SCRIPT_DIR%.
set OUT_DIR=%SCRIPT_DIR%build
set TARGET=lumc
set COMPILER=cl
set USE_CLANG=0
set VSDEV_CMD=

if /I "%~1"=="tests" set TARGET=tests
if /I "%~2"=="tests" set TARGET=tests
if /I "%~1"=="lum_to_c" set TARGET=lum_to_c
if /I "%~2"=="lum_to_c" set TARGET=lum_to_c

if /I "%~1"=="clang" set COMPILER=clang-cl
if /I "%~1"=="clang-cl" set COMPILER=clang-cl
if /I "%~1"=="/clang" set COMPILER=clang-cl
if /I "%~2"=="clang" set COMPILER=clang-cl
if /I "%~2"=="clang-cl" set COMPILER=clang-cl
if /I "%~2"=="/clang" set COMPILER=clang-cl

if /I "%COMPILER%"=="clang-cl" set USE_CLANG=1

set CONFIG=debug
if /I "%~1"=="release" set CONFIG=release
if /I "%~2"=="release" set CONFIG=release
if /I "%~3"=="release" set CONFIG=release

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

if "%USE_CLANG%"=="0" (
    where cl.exe >nul 2>&1
    if errorlevel 1 (
        for /f "usebackq delims=" %%I in (`where vswhere.exe 2^>nul`) do (
            for /f "usebackq delims=" %%J in (`"%%I" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDEV_CMD=%%J\Common7\Tools\VsDevCmd.bat"
        )
        if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
        if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
        if not defined VSDEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Insiders\Common7\Tools\VsDevCmd.bat" set "VSDEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Insiders\Common7\Tools\VsDevCmd.bat"
        if defined VSDEV_CMD (
            call "!VSDEV_CMD!" -no_logo -arch=x64 -host_arch=x64
            if errorlevel 1 exit /b !errorlevel!
        )
    )
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
set CFLAGS=%COMMON_FLAGS% /TC /D_CRT_NONSTDC_NO_DEPRECATE /DSTATIC_PLUGINS
set CXXFLAGS=%COMMON_FLAGS% /std:c++20 /D_CRT_NONSTDC_NO_DEPRECATE /DSTATIC_PLUGINS
if /I "%TARGET%"=="tests" (
    set CFLAGS=%CFLAGS% /Od /MDd
    set CXXFLAGS=%CXXFLAGS% /Od /MDd
) else (
    if /I "%CONFIG%"=="release" (
        set CFLAGS=%CFLAGS% /O2 /MD
        set CXXFLAGS=%CXXFLAGS% /O2 /MD
    ) else (
        set CFLAGS=%CFLAGS% /Od /MDd
        set CXXFLAGS=%CXXFLAGS% /Od /MDd
    )
)

if /I "%TARGET%"=="tests" (
    set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\tests.exe"
) else if /I "%TARGET%"=="lum_to_c" (
    set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\lum_to_c.exe"
) else (
    set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\lumc.exe"
)

REM Clean up stale objects from previous runs.
del /q /f "%SCRIPT_DIR%*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\*.obj" >nul 2>&1

if /I "%TARGET%"=="tests" (
	%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%tests/main.cpp" "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%capi.cpp"
	if errorlevel 1 exit /b !errorlevel!
	%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%runtime.c"
	if errorlevel 1 exit /b !errorlevel!
	%COMPILER% "main.obj" "parser.obj" "compiler.obj" "bytecode_compiler.obj" "runtime.obj" "capi.obj" /link %LDFLAGS%
) else if /I "%TARGET%"=="lum_to_c" (
	%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%c_compiler\lum_to_c.cpp" "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%c_compiler\c_compiler.cpp" "%SCRIPT_DIR%capi.cpp"
	if errorlevel 1 exit /b !errorlevel!
	%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%runtime.c"
	if errorlevel 1 exit /b !errorlevel!
	%COMPILER% "lum_to_c.obj" "parser.obj" "compiler.obj" "c_compiler.obj" "runtime.obj" "capi.obj" /link %LDFLAGS%
) else (
	if /I "%USE_CLANG%"=="1" (
		%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%lumc.c"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%capi.cpp"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%runtime.c"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% "lumc.obj" "parser.obj" "compiler.obj" "bytecode_compiler.obj" "runtime.obj" "capi.obj" /link %LDFLAGS%
	) else (
		%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%lumc.c"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% %CXXFLAGS% /c "%SCRIPT_DIR%parser.cpp" "%SCRIPT_DIR%compiler.cpp" "%SCRIPT_DIR%bytecode_compiler.cpp" "%SCRIPT_DIR%capi.cpp"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% %CFLAGS% /c "%SCRIPT_DIR%runtime.c"
		if errorlevel 1 exit /b !errorlevel!
		%COMPILER% "lumc.obj" "parser.obj" "compiler.obj" "bytecode_compiler.obj" "runtime.obj" "capi.obj" /link %LDFLAGS%
	)
)

set RESULT=%errorlevel%

REM Clean up intermediate files created by cl/cland-cl.
del /q /f "%SCRIPT_DIR%*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\*.obj" >nul 2>&1
del /q /f "%SCRIPT_DIR%vc*.pdb" >nul 2>&1
del /q /f "%SCRIPT_DIR%..\vc*.pdb" >nul 2>&1

exit /b %RESULT%
