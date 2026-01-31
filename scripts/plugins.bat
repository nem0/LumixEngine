@echo off

REM all path are relative to the script, so run in the script's directory
REM in case user runs the script from another directory
pushd %~dp0

setlocal
	set dir_3rdparty_src="../external/_repos/"
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
	echo  1. Go back
	echo  2. Empty plugin template
	echo  3. Maps
	echo  4. Shader editor
	echo  5. GLTF importer
	echo  6. Network
	echo  7. JS
	echo  8. C#
	echo  9. Visual script
	echo  A. Procedural geometry
	echo  B. Marketplace
	echo  C. LiveCode 
	echo  D. Basis Universal
	echo  E. Jolt
	echo  F. Bolt script
	echo  G. Remote control
	echo ===============================
	choice /C 123456789ABCDEFG /N /M "Your choice:"
	echo.
	if %errorlevel%==1 exit /B 0
	if %errorlevel%==2 call :empty_plugin
	if %errorlevel%==3 call :map_plugin
	if %errorlevel%==4 call :shader_editor_plugin
	if %errorlevel%==5 call :glft_import_plugin
	if %errorlevel%==6 call :network_plugin
	if %errorlevel%==7 call :js_plugin
	if %errorlevel%==8 call :cs_plugin
	if %errorlevel%==9 call :visual_script_plugin
	if %errorlevel%==10 call :procedural_geom_plugin
	if %errorlevel%==11 call :marketplace_plugin
	if %errorlevel%==12 call :livecode_plugin
	if %errorlevel%==13 call :basisu
	if %errorlevel%==14 call :jolt_plugin
	if %errorlevel%==15 call :bolt_plugin
	if %errorlevel%==16 call :remote_control
goto :begin

:glft_import_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist gltf_import (
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_gltf.git gltf_import
		pushd gltf_import
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_gltf.git
		popd
	) else (
		cd gltf_import
		git pull
	)
	popd
exit /B 0

:network_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist net (
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_net.git net
		pushd net
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_net.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_js.git js
		pushd js
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_js.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_csharp.git csharp
		pushd csharp
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_csharp.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_visualscript.git visualscript
		pushd visualscript
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_visualscript.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_shader_editor.git shader_editor
		pushd shader_editor
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_shader_editor.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_maps.git maps
		pushd maps
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_maps.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_procedural_geom.git procedural_geom
		pushd procedural_geom
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_procedural_geom.git
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
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_market.git market
		pushd market
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_market.git
		popd
	) else (
		cd market
		git pull
	)
	popd
exit /B 0

:livecode_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist livecode (
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_livecode.git livecode
		pushd livecode
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_livecode.git
		popd
	) else (
		cd livecode
		git pull
	)
	popd
exit /B 0

:jolt_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist jolt (
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_jolt.git jolt
		pushd jolt
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_jolt.git
		popd
	) else (
		cd jolt
		git pull
	)
	popd
exit /B 0

:bolt_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist bolt (
		git.exe clone https://github.com/abdulrhmandeveloper2/blackengine_bolt.git bolt
		pushd bolt
		git remote add origin2 git@github.com:abdulrhmandeveloper2/blackengine_bolt.git
		popd
	) else (
		cd bolt
		git pull
	)
	popd
exit /B 0

:remote_control
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist bolt (
		git.exe clone https://github.com/abdulrhmandeveloper2/remote_control_black.h.git remote_control
		pushd remote_control
		git remote add origin2 git@github.com:abdulrhmandeveloper2/remote_control_black.h.git
		popd
	) else (
		cd remote_control
		git pull
	)
	popd
exit /B 0

:empty_plugin
	if not exist ..\plugins mkdir ..\plugins
	pushd ..\plugins
	if not exist myplugin (
		git.exe clone https://github.com/abdulrhmandeveloper2/black.h_plugin_template.git myplugin
	) else (
		cd myplugin
		git pull
	)
	popd
exit /B 0

:basisu
	cls
	echo Basis Universal
	echo ===============================
	echo  1. Go back
	echo  2. Download
	if exist "../external/_repos/basisu/" (
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
	if %errorlevel%==5 "../external/_repos/basisu/black.h/vs2022/basis_black.h.sln"
	pause
goto :basisu

:build_basisu
	.\genie.exe --file=../external/_repos/basisu/black.h/genie.lua vs2022
	%msbuild_cmd% ..\external\_repos\basisu\black.h\vs2022\basis_black.h.sln /p:Configuration="Release" /p:Platform=x64
exit /B 0

:deploy_basisu
	echo %CD%
	del /Q ..\external\basisu\lib\win64_vs2017\release\*
	xcopy /E /Y "3rdparty\basisu\black.h\vs2022\bin\*.*" ..\external\basisu\lib\win64_vs2017\release\
	del /Q ..\external\basisu\include\*
	xcopy /E /Y "3rdparty\basisu\transcoder\*.h" ..\external\basisu\include\transcoder
	xcopy /E /Y "3rdparty\basisu\encoder\*.h" ..\external\basisu\include\encoder
exit /B 0

:download_basisu
	if not exist %dir_3rdparty_src% mkdir %dir_3rdparty_src%
	pushd %dir_3rdparty_src%
	if not exist basisu (
		git.exe clone --depth=1 https://github.com/abdulrhmandeveloper2/basis_universal.git basisu
	) else (
		cd basisu
		git pull
	)
	popd
exit /B 0

