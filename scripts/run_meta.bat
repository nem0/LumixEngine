@echo off
REM Compile and run meta for LumixEngine
REM read docs/meta.md for more information
setlocal

set SCRIPTS_DIR=%~dp0
set ROOT_DIR=%SCRIPTS_DIR%..
set SLN_PATH=%SCRIPTS_DIR%tmp\vs2022\LumixEngine.sln
set META_PATH=%SCRIPTS_DIR%tmp\vs2022\bin\Debug\meta.exe

REM Initialize Visual Studio developer environment
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
  echo Error: vswhere.exe not found. Please install Visual Studio.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VSINSTALL=%%I
if not defined VSINSTALL (
  echo Error: Visual Studio installation not found.
  exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul

REM Ensure solution exists
if not exist "%SLN_PATH%" (
  echo Solution not found, generating with genie.exe...
  "%SCRIPTS_DIR%genie.exe" vs2022
  if errorlevel 1 (
    echo Failed to generate solution.
    exit /b 1
  )
)

REM Build meta project only (faster than building entire solution)
echo Building meta...
msbuild "%SCRIPTS_DIR%tmp\vs2022\meta.vcxproj" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

REM Run meta from project root (meta scans src/ and plugins/ relative to cwd)
if not exist "%META_PATH%" (
  echo Error: meta.exe not found at %META_PATH%
  exit /b 1
)
pushd "%ROOT_DIR%"
echo Running meta...
"%META_PATH%"
if errorlevel 1 (
  echo Meta failed.
  popd
  exit /b 1
)
popd
echo Meta completed successfully.
