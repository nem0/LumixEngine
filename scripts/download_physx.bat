@echo off

REM This script downloads the PhysX source code from the official repository
REM GENie automatically detects PhysX source code and uses it instead of the prebuilt version

setlocal
	set dir_3rdparty_src="..\external\_repos\"

pushd %~dp0
	if not exist %dir_3rdparty_src% mkdir %dir_3rdparty_src%
	pushd %dir_3rdparty_src%
	if not exist physx (
		git.exe clone --depth=1 https://github.com/abdulrhmandeveloper2/PhysX.git physx
	) else (
		cd physx
		git pull
	)
	popd
popd