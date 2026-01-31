@echo off
REM Compile and run tests for black Engine

pushd %~dp0
REM Enable delayed expansion so ERRORLEVEL can be read correctly inside
REM parenthesized blocks (we capture the tests.exe exit code with !ERRORLEVEL!).
setlocal enabledelayedexpansion

REM detect msbuild
set msbuild_cmd=
where /q msbuild.exe
if %errorlevel%==0 (
  for /f "usebackq delims=" %%A in (`where msbuild.exe`) do set msbuild_cmd=%%A & goto :msbuild_found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq delims=" %%A in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set msbuild_cmd=%%A
)
if "%msbuild_cmd%"=="" (
  set msbuild_cmd=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe
)
:msbuild_found

REM Ensure solution exists
if not exist "tmp\vs2022\black Engine.sln" (
  echo Solution not found, generating with genie.exe...
  genie.exe --with-tests vs2022
  if not %errorlevel%==0 (
    echo Failed to generate solution.
    popd
    exit /b 1
  )
)

REM Build solution
echo Building solution...
REM Initialize Visual Studio developer environment if available
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSINSTALL=%%I
)
if defined VSINSTALL (
  if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
    call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
  )
)

echo Using MSBuild: %msbuild_cmd%
"%msbuild_cmd%" "tmp\vs2022\black Engine.sln" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
if not %errorlevel%==0 (
  echo Build failed.
  popd
  exit /b %errorlevel%
)

REM Run tests
if exist "tmp\vs2022\bin\Debug\tests.exe" (
  echo Running tests.exe
  "tmp\vs2022\bin\Debug\tests.exe"
  set rc=!ERRORLEVEL!
  if NOT "!rc!"=="0" (
    echo Some tests failed.
    popd
    exit /b !rc!
  )
  popd
  echo All tests completed successfully.
  exit /b 0
) else (
  echo No test executables found in %BIN_DIR%
  popd
  exit /b 2
)

