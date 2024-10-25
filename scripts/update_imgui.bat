@echo off

REM This script manages the imgui library in the external/imgui/ directory.
REM It clones or updates the imgui repository and copies the necessary source and include files to the project.
REM Run this script to ensure imgui is up-to-date in the project.

setlocal
	set dir_3rdparty_src="..\external\_repos\"
	set dst_src_dir="%~dp0..\external\imgui"

pushd %~dp0
	if not exist %dir_3rdparty_src% mkdir %dir_3rdparty_src%
	pushd %dir_3rdparty_src%
		if not exist imgui (
			git.exe clone https://github.com/ocornut/imgui imgui
			cd imgui
			git.exe checkout docking
		) else (
			cd imgui
			git.exe checkout docking
			git pull
		)
		for /f "tokens=*" %%i in ('git rev-parse HEAD') do set GIT_REVISION=%%i
		echo %GIT_REVISION% > %dst_src_dir%\imgui_revision.txt

		xcopy "*.cpp" %dst_src_dir% /y /-I
		xcopy "imgui.h" %dst_src_dir% /y /-I
		xcopy "imgui_internal.h" %dst_src_dir% /y /-I
		xcopy "imstb_rectpack.h" %dst_src_dir% /y /-I
		xcopy "imstb_textedit.h" %dst_src_dir% /y /-I
		xcopy "imstb_truetype.h" %dst_src_dir% /y /-I
	popd
popd