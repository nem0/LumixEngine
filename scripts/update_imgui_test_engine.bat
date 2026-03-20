@echo off

REM This script manages the Dear ImGui Test Engine library in the external/imgui_test_engine/ directory.
REM It clones or updates the imgui_test_engine repository and copies the engine files to the project.
REM Run this script to ensure imgui_test_engine is up-to-date in the project.

setlocal
	set "dir_3rdparty_src=%~dp0..\external\_repos\"
	set "dst_root=%~dp0..\external\imgui_test_engine"
	set "dst_src_dir=%dst_root%"

pushd "%~dp0"
	if not exist "%dir_3rdparty_src%" mkdir "%dir_3rdparty_src%"
	if not exist "%dst_root%" mkdir "%dst_root%"
	pushd "%dir_3rdparty_src%"
		if not exist imgui_test_engine (
			git.exe clone https://github.com/ocornut/imgui_test_engine imgui_test_engine
			cd imgui_test_engine
		) else (
			cd imgui_test_engine
			git pull
		)
		for /f "tokens=*" %%i in ('git rev-parse HEAD') do set GIT_REVISION=%%i
		echo %GIT_REVISION% > "%dst_root%\imgui_test_engine_revision.txt"

		if exist "%dst_root%\imgui_test_engine\" rmdir /s /q "%dst_root%\imgui_test_engine"
		xcopy "imgui_test_engine\*" "%dst_src_dir%\" /e /i /y
	popd
popd
