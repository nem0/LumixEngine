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
	echo   3. Run Studio
	echo   4. Open in VS
	echo   5. Create bundle
	echo   6. Open live help / discord chat
	echo   7. 3rd party
	echo   8. Plugins
	echo ===============================
	choice /C 12345678 /N /M "Your choice:"
	echo.

	if %errorlevel%==1 goto :EOF
	if %errorlevel%==2 call :create_project
	if %errorlevel%==3 call :run_studio
	if %errorlevel%==4 call :open_in_vs
	if %errorlevel%==5 call :create_bundle
	if %errorlevel%==6 call :open_discord
	if %errorlevel%==7 call :third_party
	if %errorlevel%==8 call :plugins
goto :begin

:plugins
	cls
	echo Wut?
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
	echo  B. Visual script
	echo  C. Procedural geometry
	echo  D. Marketplace
	echo ===============================
	choice /C 123456789ABCD /N /M "Your choice:"
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
	if %errorlevel%==11 call :visual_script_plugin
	if %errorlevel%==12 call :procedural_geom_plugin
	if %errorlevel%==13 call :marketplace_plugin
	call :create_project
goto :plugins

:dx_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist dx (
		git.exe clone https://github.com/nem0/lumixengine_dx.git dx
		pushd dx
		git remote add origin2 git@github.com:nem0/lumixengine_dx.git
		popd
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
		pushd gltf_import
		git remote add origin2 git@github.com:nem0/lumixengine_gltf.git
		popd
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
		pushd rml
		git remote add origin2 git@github.com:nem0/lumixengine_rml.git
		popd
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
		pushd net
		git remote add origin2 git@github.com:nem0/lumixengine_net.git
		popd
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
		pushd js
		git remote add origin2 git@github.com:nem0/lumixengine_js.git
		popd
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
		pushd csharp
		git remote add origin2 git@github.com:nem0/lumixengine_csharp.git
		popd
	) else (
		cd csharp
		git pull
	)
	popd
exit /B 0

:visual_script_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist visualscript (
		git.exe clone https://github.com/nem0/lumixengine_visualscript.git visualscript
		pushd visualscript
		git remote add origin2 git@github.com:nem0/lumixengine_visualscript.git
		popd
	) else (
		cd visualscript
		git pull
	)
	popd
exit /B 0

:shader_editor_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist shader_editor (
		git.exe clone https://github.com/nem0/lumixengine_shader_editor.git shader_editor
		pushd shader_editor
		git remote add origin2 git@github.com:nem0/lumixengine_shader_editor.git
		popd
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
		pushd maps
		git remote add origin2 git@github.com:nem0/lumixengine_maps.git
		popd
	) else (
		cd maps
		git pull
	)
	popd
exit /B 0

:procedural_geom_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist procedural_geom (
		git.exe clone https://github.com/nem0/lumixengine_procedural_geom.git procedural_geom
		pushd procedural_geom
		git remote add origin2 git@github.com:nem0/lumixengine_procedural_geom.git
		popd
	) else (
		cd procedural_geom
		git pull
	)
	popd
exit /B 0

:marketplace_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist market (
		git.exe clone https://github.com/nem0/lumixengine_market.git market
		pushd market
		git remote add origin2 git@github.com:nem0/lumixengine_market.git
		popd
	) else (
		cd market
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
	echo  3. Recast navigation
	echo  4. PhysX
	echo  5. LuaJIT
	echo  6. FreeType2
	echo  7. Basis Universal
	echo ===============================
	choice /C 1234567 /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :all_3rdparty
	if %errorlevel%==3 call :recast
	if %errorlevel%==4 call :physx
	if %errorlevel%==5 call :luajit
	if %errorlevel%==6 call :freetype
	if %errorlevel%==7 call :basisu
goto :third_party

:all_3rdparty
	call :download_physx
	call :download_recast
	call :download_luajit
	call :download_freetype
	call :download_basisu
	
	call :build_physx
	call :build_recast
	call :build_luajit
	call :build_freetype
	call :build_basisu
	
	call :deploy_physx
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
	if %errorlevel%==5 start "" %devenv_cmd% "3rdparty\basisu\lumix\vs2022\basis_lumix.sln"
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
		..\..\..\genie.exe vs2022
	popd
	%msbuild_cmd% 3rdparty\basisu\lumix\vs2022\basis_lumix.sln /p:Configuration="Release" /p:Platform=x64
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
	xcopy /E /Y "3rdparty\basisu\lumix\vs2022\bin\*.*" ..\external\basisu\lib\win64_vs2017\release\
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
	genie.exe --file=recastnavigation.lua vs2022
	%msbuild_cmd% 3rdparty\recast\_project\RecastDetour.sln /p:Configuration=Release /p:Platform=x64
exit /B 0

:create_project
	echo Creating project...
	genie.exe --with-app vs2022 
	pause
exit /B 0

:build
	if not exist "tmp/vs2022/LumixEngine.sln" call :create_project
	echo Building...
	%msbuild_cmd% tmp/vs2022/LumixEngine.sln /p:Configuration=RelWithDebInfo
	pause
exit /B 0

:run_studio
	if not exist "tmp/vs2022/bin/RelWithDebInfo/studio.exe" call :build
	cd ..\data
	start "" "../projects/tmp/vs2022/bin/RelWithDebInfo/studio.exe"
	cd ..\projects
	pause
exit /B 0

:open_in_vs
	"tmp/vs2022/LumixEngine.sln"
exit /B 0

:create_bundle
	echo Creating bundle...
	genie.exe --embed-resources --static-physx vs2022
	cd ..\data
	tar -cvf data.tar .
	move data.tar ../src/studio
	cd ..\projects\
	%msbuild_cmd% tmp/vs2022/LumixEngine.sln /p:Configuration=RelWithDebInfo
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

:open_discord
	start "" "https://discord.gg/RgFybs6"
	pause
exit /B 0

