@echo off
REM build.bat - Build lumc.exe standalone LumScript runner using cl.exe
REM
REM Requirements:
REM   - Visual Studio Developer Command Prompt (or vcvarsall.bat sourced)
REM   - cl.exe must be in PATH
REM
REM Usage:
REM   build.bat          - Build Release configuration
REM   build.bat debug    - Build Debug configuration

setlocal

REM Determine build configuration
set CONFIG=Release
if /i "%1"=="debug" set CONFIG=Debug

REM Set up paths
set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%..\..
set SRC_DIR=%ROOT_DIR%\src
set EXT_DIR=%ROOT_DIR%\external
set OUT_DIR=%SCRIPT_DIR%build

REM Create output directory
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

REM Set up x64 build environment using vcvarsall.bat
set "VCVARSALL="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"

if defined VCVARSALL call "%VCVARSALL%" x64 >nul 2>&1

REM Verify x64 cl.exe is available
cl.exe 2>&1 | findstr /C:"for x64" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Warning: cl.exe is not targeting x64.
    echo Please run from x64 Native Tools Command Prompt for VS 2022.
    echo Trying to continue anyway...
)

REM Compiler flags
set CFLAGS=/nologo /std:c++20 /EHsc /Zi /I"%SRC_DIR%" /I"%EXT_DIR%"
set CFLAGS=%CFLAGS% /D_CRT_NONSTDC_NO_DEPRECATE
set CFLAGS=%CFLAGS% /DSTATIC_PLUGINS

if /i "%CONFIG%"=="Debug" (
    set CFLAGS=%CFLAGS% /Od /MDd /DLUMIX_DEBUG
) else (
    set CFLAGS=%CFLAGS% /O2 /MD
)

REM Linker flags
set LDFLAGS=/nologo /DEBUG /INCREMENTAL:NO /OUT:"%OUT_DIR%\lumc.exe"

echo Building lumc [%CONFIG%]...
echo.

REM Compile
cl %CFLAGS% "%SCRIPT_DIR%lumc.cpp" /link %LDFLAGS%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    exit /b 1
)

echo.
echo Build succeeded: %OUT_DIR%\lumc.exe
echo.

REM Clean up intermediate files
if exist "lumc.obj" del "lumc.obj"
if exist "vc*.pdb" del "vc*.pdb"
if exist "vc*.idb" del "vc*.idb"

endlocal
