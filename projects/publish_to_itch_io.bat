REM init developer command prompt
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"

set msbuild_cmd=msbuild.exe
set devenv_cmd=devenv.exe
where /q devenv.exe
if not %errorlevel%==0 set devenv_cmd="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe"
where /q msbuild.exe
if not %errorlevel%==0 set msbuild_cmd="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"

REM put butler in path or in ..\butler\
SET PATH=%PATH%;..\..\butler\

REM clean everything
git.exe clean -f -x -d ..\
if not %errorlevel%==0 pause

REM download physx
mkdir 3rdparty
if not %errorlevel%==0 pause

cd 3rdparty
git.exe clone --depth=1 https://github.com/nem0/PhysX.git physx
if not %errorlevel%==0 pause

REM build static physx
cd PhysX\physx
call generate_projects.bat lumix_vc17win64_static
if not %errorlevel%==0 pause

%msbuild_cmd% "compiler\vc17win64\PhysXSDK.sln" /p:Configuration=Release /p:Platform=x64
if not %errorlevel%==0 pause

REM deploy physx
cd ..\..\..\
if not exist "..\external\physx\lib\vs2017\win64\release_static\" mkdir ..\external\physx\lib\vs2017\win64\release_static\
del /Q ..\external\physx\lib\vs2017\win64\release_static\*
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCharacterKinematic_static_64.lib ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCommon_static_64.lib			   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCooking_static_64.lib			   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXExtensions_static_64.lib		   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXFoundation_static_64.lib		   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXPvdSDK_static_64.lib			   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXVehicle_static_64.lib			   ..\external\physx\lib\vs2017\win64\release_static\
copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysX_static_64.lib				   ..\external\physx\lib\vs2017\win64\release_static\

REM download dx
pushd ..\plugins
git.exe clone https://github.com/nem0/lumixengine_dx.git dx
if not %errorlevel%==0 pause
popd

REM create engine project
bin/windows/genie.exe --static-physx --with-app --nodx vs2022
if not %errorlevel%==0 pause

REM build studio.exe
%msbuild_cmd% tmp/vs2022/LumixEngine.sln /p:Configuration=RelWithDebInfo
if not %errorlevel%==0 pause

REM push gl version
mkdir itch_io
copy tmp\vs2022\bin\RelWithDebInfo\studio.exe itch_io\
copy tmp\vs2022\bin\RelWithDebInfo\app.exe itch_io\
copy .itch.toml itch_io\
git clean -f -x -d ..\data
xcopy /E /Y ..\data itch_io
butler.exe push itch_io mikulasflorek/lumix-engine:win-64-gl
if not %errorlevel%==0 pause

REM create engine project
bin/windows/genie.exe --static-physx --with-app vs2022
if not %errorlevel%==0 pause

REM build studio.exe
%msbuild_cmd% tmp/vs2022/LumixEngine.sln /p:Configuration=RelWithDebInfo
if not %errorlevel%==0 pause

REM push gl version
mkdir itch_io
copy tmp\vs2022\bin\RelWithDebInfo\studio.exe itch_io\
copy tmp\vs2022\bin\RelWithDebInfo\app.exe itch_io\
copy .itch.toml itch_io\
git clean -f -x -d ..\data
xcopy /E /Y ..\data itch_io
butler.exe push itch_io mikulasflorek/lumix-engine:win-64-dx
pause
