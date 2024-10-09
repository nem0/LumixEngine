@echo off

REM we vendor recast in external/recast/src/*
REM download/pull latest recast
REM copy src and include files from recast repo to the project
REM call this to update recast in the project

setlocal
	set dir_3rdparty_src="..\external\_repos\"
	set dst_src_dir="%~dp0..\external\recast\src"
	set dst_inc_dir="%~dp0..\external\recast\include"

pushd %~dp0
	if not exist %dir_3rdparty_src% mkdir %dir_3rdparty_src%
	pushd %dir_3rdparty_src%
		if not exist recast (
			git.exe clone --depth=1  https://github.com/nem0/recastnavigation.git recast
			cd recast
		) else (
			cd recast
			git pull
		)

		for /f "tokens=*" %%i in ('git rev-parse HEAD') do set GIT_REVISION=%%i
		echo %GIT_REVISION% > %dst_src_dir%\recast_revision.txt

		xcopy "Detour\Source\DetourProximityGrid.cpp" %dst_src_dir% /y
		
		xcopy "Detour\Source\*" %dst_src_dir% /y
		xcopy "DetourCrowd\Source\*" %dst_src_dir% /y
		xcopy "Recast\Source\*" %dst_src_dir% /y

		xcopy "Detour\Include\*" %dst_inc_dir% /y
		xcopy "DetourCrowd\Include\*" %dst_inc_dir% /y
		xcopy "Recast\Include\*" %dst_inc_dir% /y
	popd
popd