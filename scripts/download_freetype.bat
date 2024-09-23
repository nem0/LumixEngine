@echo off

REM This script downloads the FreeType2 source code from the official repository

setlocal 
	
setlocal
	set dir_3rdparty_src="..\external\_repos\"

pushd %~dp0
	if not exist %dir_3rdparty_src% mkdir %dir_3rdparty_src%
	pushd %dir_3rdparty_src%
	if not exist freetype (
		git.exe clone --depth=1 https://github.com/nem0/freetype2.git freetype
	) else (
		cd freetype
		git pull
	)
	popd
popd