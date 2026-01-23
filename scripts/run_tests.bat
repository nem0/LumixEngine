@echo off
REM Compile and run tests for LumixEngine

pushd %~dp0
REM Enable delayed expansion so ERRORLEVEL can be read correctly inside
REM parenthesized blocks (we capture the tests.exe exit code with !ERRORLEVEL!).
setlocal enabledelayedexpansion

REM detect msbuild
set msbuild_cmd=msbuild.exe
where /q msbuild.exe
if not %errorlevel%==0 (
  set msbuild_cmd="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
)

REM Ensure solution exists
if not exist "tmp\vs2022\LumixEngine.sln" (
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
echo Current directory: %cd%
echo msbuild command: %msbuild_cmd%
where msbuild.exe || echo msbuild.exe not found on PATH
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -prerelease -format text
) else (
  echo vswhere not found
)
echo Invoking msbuild with diagnostic verbosity to capture detailed errors...
%msbuild_cmd% "tmp\vs2022\LumixEngine.sln" /p:Configuration=Debug /p:Platform=x64 /verbosity:diag > build_log.txt 2>&1
type build_log.txt
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

