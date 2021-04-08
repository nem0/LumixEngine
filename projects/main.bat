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
	echo   9. 3rd party
	echo   A. Plugins
	echo   B. Create project - static physx
	echo   C. Push to itch.io
	echo   D. Push to itch.io (DX11)
	echo   E. Create project with app 
	echo ===============================
	choice /C 123456789ABCDE /N /M "Your choice:"
	echo.

	if %errorlevel%==1 goto :EOF
	if %errorlevel%==2 call :create_project
	if %errorlevel%==3 call :build
	if %errorlevel%==4 call :run_studio
	if %errorlevel%==5 call :open_in_vs
	if %errorlevel%==6 call :create_bundle
	if %errorlevel%==7 call :git_pull
	if %errorlevel%==8 call :open_discord
	if %errorlevel%==9 call :third_party
	if %errorlevel%==10 call :plugins
	if %errorlevel%==11 call :create_project_static_physx
	if %errorlevel%==12 call :push_to_itch_io
	if %errorlevel%==13 call :push_to_itch_io_dx
	if %errorlevel%==14 call :create_project_with_app
goto :begin

:plugins 
	cls
	echo Wut? - you have to (re)create project if you download/upgrade plugin
	echo ===============================
	echo  1. Go back
	echo  2. Empty plugin template
	echo  3. Maps
	echo  4. Shader editor
	echo  5. GLTF importer
	echo  6. DX11 and DX12
	echo  7. Rml UI
	echo  8. Network
	echo  9. JS
	echo  A. C#
	echo ===============================
	choice /C 123456789 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :empty_plugin
	if %errorlevel%==3 call :map_plugin
	if %errorlevel%==4 call :shader_editor_plugin
	if %errorlevel%==5 call :glft_import_plugin
	if %errorlevel%==6 call :dx_plugin
	if %errorlevel%==7 call :rml_ui_plugin
	if %errorlevel%==8 call :network_plugin
	if %errorlevel%==9 call :js_plugin
	if %errorlevel%==10 call :cs_plugin
	pause
goto :plugins

:push_to_itch_io
	if not exist itch_io mkdir itch_io
	copy tmp\vs2019\bin\RelWithDebInfo\studio.exe itch_io\
	git clean -f -x -d ..\data
	xcopy /E /Y ..\data itch_io
	butler.exe push itch_io mikulasflorek/lumix-engine:win-64-gl
	pause
exit /B 0

:push_to_itch_io_dx
	if not exist itch_io mkdir itch_io
	copy tmp\vs2019\bin\RelWithDebInfo\studio.exe itch_io\
	git clean -f -x -d ..\data
	xcopy /E /Y ..\data itch_io
	butler.exe push itch_io mikulasflorek/lumix-engine:win-64-dx
	pause
exit /B 0

:dx_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist dx (
		git.exe clone https://github.com/nem0/lumixengine_dx.git dx
	) else (
		cd dx
		git pull
	)
	popd
exit /B 0

:glft_import_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist gltf_import (
		git.exe clone https://github.com/nem0/lumixengine_gltf.git gltf_import
	) else (
		cd gltf_import
		git pull
	)
	popd
exit /B 0

:rml_ui_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist rml (
		git.exe clone https://github.com/nem0/lumixengine_rml.git rml
	) else (
		cd rml
		git pull
	)
	popd
exit /B 0

:network_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist net (
		git.exe clone https://github.com/nem0/lumixengine_net.git net
	) else (
		cd net
		git pull
	)
	popd
exit /B 0

:js_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist js (
		git.exe clone https://github.com/nem0/lumixengine_js.git js
	) else (
		cd js
		git pull
	)
	popd
exit /B 0

:cs_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist csharp (
		git.exe clone https://github.com/nem0/lumixengine_csharp.git csharp
	) else (
		cd csharp
		git pull
	)
	popd
exit /B 0

:shader_editor_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist shader_editor (
		git.exe clone https://github.com/nem0/lumixengine_shader_editor.git shader_editor
	) else (
		cd shader_editor
		git pull
	)
	popd
exit /B 0

:map_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist maps (
		git.exe clone https://github.com/nem0/lumixengine_maps.git maps
	) else (
		cd maps
		git pull
	)
	popd
exit /B 0

:empty_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist myplugin (
		git.exe clone https://github.com/nem0/lumix_plugin_template.git myplugin
	) else (
		cd myplugin
		git pull
	)
	popd
exit /B 0

:third_party 
	REM we should use specific 3rd party revision
	cls
	echo Wut2?
	echo ===============================
	echo  1. Go back
	echo  2. Download, build and deploy all
	echo  3. NVTT
	echo  4. Recast navigation
	echo  5. PhysX
	echo  6. LuaJIT
	echo  7. FreeType2
	echo  8. Basis Universal
	echo ===============================
	choice /C 12345678 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :all_3rdparty
	if %errorlevel%==3 call :nvtt
	if %errorlevel%==4 call :recast
	if %errorlevel%==5 call :physx
	if %errorlevel%==6 call :luajit
	if %errorlevel%==7 call :freetype
	if %errorlevel%==8 call :basisu
goto :third_party

:all_3rdparty
	call :download_physx
	call :download_nvtt
	call :download_recast
	call :download_luajit
	call :download_freetype
	call :download_basisu
	
	call :build_physx
	call :build_nvtt
	call :build_recast
	call :build_luajit
	call :build_freetype
	call :build_basisu
	
	call :deploy_physx
	call :deploy_nvtt
	call :deploy_recast
	call :deploy_luajit
	call :deploy_freetype
	call :deploy_basisu
	pause

exit /B 0

:luajit
	cls
	echo LuaJIT
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\luajit\" (
		echo  3. Build
		echo  4. Deploy
	)
	echo ===============================
	choice /C 1234 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_luajit
	if %errorlevel%==3 call :build_luajit
	if %errorlevel%==4 call :deploy_luajit
	pause
goto :luajit

:deploy_luajit
	del /Q ..\external\luajit\lib\win64_vs2017\release\*
	del /Q ..\external\luajit\include\*
	copy 3rdparty\luajit\src\lua51.lib ..\external\luajit\lib\win64_vs2017\release\
	copy 3rdparty\luajit\src\luajit.lib ..\external\luajit\lib\win64_vs2017\release\
	copy 3rdparty\luajit\src\lauxlib.h ..\external\luajit\include
	copy 3rdparty\luajit\src\lua.h ..\external\luajit\include
	copy 3rdparty\luajit\src\lua.hpp ..\external\luajit\include
	copy 3rdparty\luajit\src\luaconf.h ..\external\luajit\include
	copy 3rdparty\luajit\src\luajit.h ..\external\luajit\include
	copy 3rdparty\luajit\src\lualib.h ..\external\luajit\include
exit /B 0

:build_luajit
	pushd 3rdparty\luajit\src
	call msvcbuild.bat static
	popd
exit /B 0

:download_luajit
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist luajit (
		git.exe clone https://github.com/nem0/LuaJIT.git luajit
	) else (
		cd luajit
		git pull
		cd ..
	)
	cd ..
exit /B 0

:basisu
	cls
	echo Basis Universal
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\basisu\" (
		echo  3. Build
		echo  4. Deploy
		echo  5. Open in VS
	)
	echo ===============================
	choice /C 12345 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_basisu
	if %errorlevel%==3 call :build_basisu
	if %errorlevel%==4 call :deploy_basisu
	if %errorlevel%==5 start "" %devenv_cmd% "3rdparty\basisu\lumix\vs2019\basis_lumix.sln"
	pause
goto :basisu

:freetype
	cls
	echo FreeType2
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\freetype\" (
		echo  3. Build
		echo  4. Deploy
		echo  5. Open in VS
	)
	echo ===============================
	choice /C 12345 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_freetype
	if %errorlevel%==3 call :build_freetype
	if %errorlevel%==4 call :deploy_freetype
	if %errorlevel%==5 start "" %devenv_cmd% "3rdparty\freetype\builds\windows\vc2010\freetype.sln"
	pause
goto :freetype

:build_freetype
	%msbuild_cmd% 3rdparty\freetype\builds\windows\vc2010\freetype.sln /p:Configuration="Release Static" /p:Platform=x64
exit /B 0

:build_basisu
	pushd 3rdparty\basisu\lumix\
		..\..\..\genie.exe vs2019
	popd
	%msbuild_cmd% 3rdparty\basisu\lumix\vs2019\basis_lumix.sln /p:Configuration="Release" /p:Platform=x64
exit /B 0

:deploy_freetype
	echo %CD%
	del /Q ..\external\freetype\lib\win64_vs2017\release\*
	copy "3rdparty\freetype\objs\x64\Release Static\freetype.lib" ..\external\freetype\lib\win64_vs2017\release\
	copy "3rdparty\freetype\objs\x64\Release Static\freetype.pdb" ..\external\freetype\lib\win64_vs2017\release\
	del /Q ..\external\freetype\include\*
	xcopy /E /Y "3rdparty\freetype\include\*" ..\external\freetype\include\
exit /B 0

:deploy_basisu
	echo %CD%
	del /Q ..\external\basisu\lib\win64_vs2017\release\*
	xcopy /E /Y "3rdparty\basisu\lumix\vs2019\bin\*.*" ..\external\basisu\lib\win64_vs2017\release\
	del /Q ..\external\basisu\include\*
	xcopy /E /Y "3rdparty\basisu\transcoder\*.h" ..\external\basisu\include\transcoder
	xcopy /E /Y "3rdparty\basisu\encoder\*.h" ..\external\basisu\include\encoder
exit /B 0

:physx
	cls
	echo PhysX
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\PhysX\" (
		echo  3. Build
		echo  4. Build and deploy static
		echo  5. Deploy
		echo  6. Open in VS
	)
	echo ===============================
	choice /C 123456 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_physx
	if %errorlevel%==3 call :build_physx
	if %errorlevel%==4 call :build_deploy_physx_static
	if %errorlevel%==5 call :deploy_physx
	if %errorlevel%==6 start "" %devenv_cmd% "3rdparty\PhysX\physx\compiler\vc15win64\PhysXSDK.sln"
	pause
goto :physx

:deploy_physx
	REM lib
	del /Q ..\external\physx\lib\vs2017\win64\release\*
	copy 3rdparty\PhysX\physx\compiler\vc15win64\sdk_source_bin\FastXml.dir\release\FastXml.lib ..\external\physx\lib\vs2017\win64\release\FastXml_static_64.lib 
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\LowLevelAABB_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\LowLevelDynamics_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\LowLevel_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCharacterKinematic_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCommon_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCooking_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXExtensions_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXFoundation_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXPvdSDK_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXTask_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXVehicle_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysX_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\SceneQuery_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\SimulationController_static_64.lib ..\external\physx\lib\vs2017\win64\release\
	REM include
	for /D %%e in (..\external\physx\include\*) do rmdir /Q /S %%e
	del /Q ..\external\physx\include\*
	xcopy /E /Y 3rdparty\PhysX\physx\include\* ..\external\physx\include\
	xcopy /E /Y 3rdparty\PhysX\pxshared\include\* ..\external\physx\include\
	REM dll
	del /Q ..\external\physx\dll\vs2017\win64\release\*
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCommon_64.dll ..\external\physx\dll\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXCooking_64.dll ..\external\physx\dll\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysXFoundation_64.dll ..\external\physx\dll\vs2017\win64\release\
	copy 3rdparty\PhysX\physx\bin\win.x86_64.vc141.md\release\PhysX_64.dll ..\external\physx\dll\vs2017\win64\release\
exit /B 0

:build_physx
	cd 3rdparty\PhysX\physx
	call generate_projects.bat lumix_vc15win64
	%msbuild_cmd% "compiler\vc15win64\PhysXSDK.sln" /p:Configuration=Release /p:Platform=x64
	cd ..\..\..\
exit /B 0

:build_deploy_physx_static
	cd 3rdparty\PhysX\physx
	call generate_projects.bat lumix_vc16win64_static
	%msbuild_cmd% "compiler\vc16win64\PhysXSDK.sln" /p:Configuration=Release /p:Platform=x64
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

exit /B 0

:recast
	cls
	echo Recast ^& Detour
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\recast\" (
		echo  3. Build
		echo  4. Deploy
		echo  5. Open in VS
	)
	echo ===============================
	choice /C 12345 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_recast
	if %errorlevel%==3 call :build_recast
	if %errorlevel%==4 call :deploy_recast
	if %errorlevel%==5 start "" %devenv_cmd% "3rdparty\recast\_project\RecastDetour.sln"
	pause
goto :recast

:deploy_recast
	del /Q ..\external\recast\include\*
	del /Q ..\external\recast\src\*
	copy 3rdparty\recast\Recast\Include\* ..\external\recast\include\
	copy 3rdparty\recast\Detour\Include\* ..\external\recast\include\
	copy 3rdparty\recast\DetourCrowd\Include\* ..\external\recast\include\
	copy 3rdparty\recast\DetourCrowd\Source\* ..\external\recast\src\
	copy 3rdparty\recast\DebugUtils\Include\* ..\external\recast\include\
	copy 3rdparty\recast\_build\Recast.lib ..\external\recast\lib\win64_vs2017\release\recast.lib
	copy 3rdparty\recast\_build\Recast.pdb ..\external\recast\lib\win64_vs2017\release\recast.pdb
exit /B 0

:build_recast
	genie.exe --file=recastnavigation.lua vs2019
	%msbuild_cmd% 3rdparty\recast\_project\RecastDetour.sln /p:Configuration=Release /p:Platform=x64
exit /B 0

:nvtt
	cls
	echo NVTT
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "3rdparty\nvtt\" (
		echo  3. Build
		echo  4. Deploy
		echo  5. Open in VS
	)
	echo ===============================
	choice /C 12345 /N /M "Your choice:"
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :download_nvtt
	if %errorlevel%==3 call :build_nvtt
	if %errorlevel%==4 call :deploy_nvtt
	if %errorlevel%==5 start "" %devenv_cmd% "3rdparty\nvtt\project\vc2017\nvtt.sln"
	pause
goto :nvtt

:deploy_nvtt
	del /Q ..\external\nvtt\include\*
	copy 3rdparty\nvtt\src\nvtt\nvtt.h ..\external\nvtt\include\
	copy 3rdparty\nvtt\project\vc2017\Release.x64\bin\nvtt.lib  ..\external\nvtt\lib\win64_vs2017\release\nvtt.lib
exit /B 0

:build_nvtt
	%msbuild_cmd% 3rdparty\nvtt\project\vc2017\nvtt.sln /p:Configuration=Release /p:Platform=x64
exit /B 0

:create_project
	echo Creating project...
	genie.exe vs2019 
	pause
exit /B 0

:create_project_with_app
	echo Creating project...
	genie.exe --with-app vs2019 
	pause
exit /B 0

:create_project_static_physx
	echo Creating project with statically linked PhysX...
	genie.exe --static-physx vs2019 
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
	genie.exe --embed-resources --static-physx vs2019
	cd ..\data
	tar -cvf data.tar .
	move data.tar ../src/studio
	cd ..\projects\
	%msbuild_cmd% tmp/vs2019/LumixEngine.sln /p:Configuration=RelWithDebInfo
	del ..\src\studio\data.tar
	pause
exit /B 0

:download_freetype
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist freetype (
		git.exe clone --depth=1 https://github.com/nem0/freetype2.git freetype
	) else (
		cd freetype
		git pull
		cd ..
	)
	cd ..
exit /B 0

:download_basisu
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist basisu (
		git.exe clone --depth=1 https://github.com/nem0/basis_universal.git basisu
	) else (
		cd basisu
		git pull
		cd ..
	)
	cd ..
exit /B 0

:download_recast
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist recast (
		git.exe clone --depth=1  https://github.com/nem0/recastnavigation.git recast
	) else (
		cd recast
		git pull
		cd ..
	)
	cd ..
exit /B 0

:download_physx
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist physx (
		git.exe clone --depth=1 https://github.com/nem0/PhysX.git physx
	) else (
		cd physx
		git pull
		cd ..
	)
	cd ..
exit /B 0

:download_nvtt
	if not exist 3rdparty mkdir 3rdparty
	cd 3rdparty
	if not exist nvtt (
		git.exe clone --depth=1 https://github.com/nem0/nvidia-texture-tools.git nvtt
	) else (
		cd nvtt
		git pull
		cd ..
	)
	cd ..
exit /B 0

:open_discord
	start "" "https://discord.gg/RgFybs6"
	pause
exit /B 0

:git_pull
	git.exe pull
exit /B 0



