@echo off
setlocal

REM Simple build+run script for the performance test (Release)

REM Optional first argument overrides generated line count (PERFT_LINES)
if not "%1"=="" set PERFT_LINES=%1

set SCRIPT_DIR=%~dp0
set OUT_DIR=%SCRIPT_DIR%build
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

REM Find vcvarsall if available
set "VCVARSALL="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"

if defined VCVARSALL call "%VCVARSALL%" x64 >nul 2>&1

REM Compiler flags (Release)
set CFLAGS=/nologo /std:c++20 /EHsc /O2 /MD /I"%SCRIPT_DIR%.."

set LDFLAGS=/nologo /OUT:"%OUT_DIR%\perftest.exe"

echo Building perftest (Release)...

cl %CFLAGS% "%SCRIPT_DIR%performance.cpp" "%SCRIPT_DIR%\..\parser.cpp" "%SCRIPT_DIR%\..\compiler.cpp" "%SCRIPT_DIR%\..\bytecode_compiler.cpp" "%SCRIPT_DIR%\..\runtime.cpp" "%SCRIPT_DIR%\..\capi.cpp" /link %LDFLAGS%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    exit /b 1
)

echo.
echo Running perftest...
"%OUT_DIR%\perftest.exe"

endlocal

REM Clean up intermediate files created by cl
echo Cleaning intermediate object files...
cmd /c "del /q /f "%SCRIPT_DIR%*.obj" >nul 2>&1 || rem no objs in tests"
cmd /c "del /q /f "%SCRIPT_DIR%..\*.obj" >nul 2>&1 || rem no objs in parent"
cmd /c "if exist "%SCRIPT_DIR%vc*.pdb" del /q "%SCRIPT_DIR%vc*.pdb" >nul 2>&1"
cmd /c "if exist "%SCRIPT_DIR%..\vc*.pdb" del /q "%SCRIPT_DIR%..\vc*.pdb" >nul 2>&1"
