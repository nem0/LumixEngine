@echo off
REM Build and run Studio with ImGui Test Engine enabled (if available).

pushd %~dp0
setlocal enabledelayedexpansion

set CONFIG=Debug
set APP_ARGS=
if /I "%~1"=="Debug" (
	set CONFIG=Debug
	shift
) else if /I "%~1"=="RelWithDebInfo" (
	set CONFIG=RelWithDebInfo
	shift
)

:collect_args
if "%~1"=="" goto args_done
set APP_ARGS=!APP_ARGS! %1
shift
goto collect_args
:args_done

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

genie.exe --with-tests vs2022
if not %errorlevel%==0 (
	echo Failed to generate solution.
	popd
	exit /b 1
)

REM Initialize Visual Studio developer environment if available
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
	for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSINSTALL=%%I
)
if defined VSINSTALL (
	if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
		call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
	)
)

echo Building studio (%CONFIG%)...
echo Using MSBuild: %msbuild_cmd%
"%msbuild_cmd%" "tmp\vs2022\studio.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 /verbosity:minimal
if not %errorlevel%==0 (
	echo Build failed.
	popd
	exit /b %errorlevel%
)

set STUDIO_EXE=tmp\vs2022\bin\%CONFIG%\studio.exe
if not exist "%STUDIO_EXE%" (
	echo Missing %STUDIO_EXE%
	popd
	exit /b 3
)

for %%I in ("%~dp0..") do set REPO_ROOT=%%~fI
set DATA_DIR=%REPO_ROOT%\data

echo Running %STUDIO_EXE%
"%STUDIO_EXE%" -imgui_test_ui -data_dir "%DATA_DIR%" !APP_ARGS!
set rc=!ERRORLEVEL!

popd
exit /b !rc!
