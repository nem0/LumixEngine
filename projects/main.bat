@echo off

REM detect paths
set msbuild_cmd=msbuild.exe
set devenv_cmd=devenv.exe
where /q devenv.exe
if not %errorlevel%==0 set devenv_cmd="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe"
where /q msbuild.exe
if not %errorlevel%==0 set msbuild_cmd="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"

:begin
cls

echo Wut?
echo ===============================
echo   1. Exit
echo   2. Create project
echo   3. Build release
echo   4. Run Studio
echo   5. Open in VS
echo   6. Create bundle
echo   7. Pull latest from Github
echo   8. Open chat
echo   9. Download Godot Engine
echo ===============================
choice /C 123456789 /N /M "Your choice:"
echo.

if %errorlevel%==1 goto :exit
if %errorlevel%==2 call :create_project
if %errorlevel%==3 call :build
if %errorlevel%==4 call :run_studio
if %errorlevel%==5 call :open_in_vs
if %errorlevel%==6 call :create_bundle
if %errorlevel%==7 call :git_pull
if %errorlevel%==8 call :open_gitter
if %errorlevel%==9 call :download_godot
goto :begin

:create_project
	echo Creating project...
	genie.exe --static-plugins vs2019
	pause
exit /B 0

:build
	if not exist "tmp/vs2019/LumixEngine.sln" call :create_project
	echo Building...
	%msbuild_cmd% tmp/vs2019/LumixEngine.sln /p:Configuration=RelWithDebInfo
	pause
exit /B 0

:run_studio
	if not exist "tmp/vs2019/bin/RelWithDebInfo/studio.exe" call :build
	cd ..\data
	start "" "../projects/tmp/vs2019/bin/RelWithDebInfo/studio.exe"
	cd ..\projects
	pause
exit /B 0

:open_in_vs
	start "" %devenv_cmd% "tmp/vs2019/LumixEngine.sln"
exit /B 0

:create_bundle
	echo Creating bundle...
	genie.exe --embed-resources --static-plugins vs2019
	cd ..\data
	tar -cvf data.tar .
	move data.tar ../src/studio
	cd ..\projects\
	%msbuild_cmd% tmp/vs2019/LumixEngine.sln /p:Configuration=RelWithDebInfo
	del ..\src\studio\data.tar
	pause
exit /B 0

:download_godot
	if %RANDOM% gtr 16000 (
		start "" "https://godotengine.org/"
		echo Son, I'm disappointed.
	) else (
		echo No.
	)	
	pause
exit /B 0

:open_gitter
	start "" "https://gitter.im/nem0/LumixEngine"
	pause
exit /B 0

:git_pull
	git pull
exit /B 0

:exit


