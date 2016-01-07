local BINARY_DIR = "tmp/bin/"
local ide_dir = iif(_ACTION == nil, "vs2013", _ACTION)

newaction {
	trigger = "install",
	description = "Install in ../../LumixEngine_data/bin",
	execute = function()
		local src_dir = path.join(BINARY_DIR, "RelWithDebInfo/")
		local dst_dir = "../../LumixEngine_data/bin/"
		
		function installDll(filename)
			os.copyfile(path.join(src_dir, filename .. ".dll"), path.join(dst_dir, filename .. ".dll"))
		end

		function installDllWithPdb(filename)
			installDll(filename)
			os.copyfile(path.join(src_dir, filename .. ".pdb"), path.join(dst_dir, filename .. ".pdb"))
		end
		
		installDllWithPdb "animation"
		installDllWithPdb "audio"
		installDllWithPdb "editor"
		installDllWithPdb "engine"
		installDllWithPdb "lua_script"
		installDllWithPdb "physics"
		installDllWithPdb "renderer"
		
		installDll "PhysX3CommonCHECKED_x64"
		installDll "PhysX3CookingCHECKED_x64"
		installDll "PhysX3CharacterKinematicCHECKED_x64"
		installDll "PhysX3CHECKED_x64"
		installDll "nvToolsExt64_1"
		installDll "assimp"

		os.copyfile(path.join(src_dir, "studio.exe"), path.join(dst_dir, "studio.exe"))
		os.copyfile(path.join(src_dir, "studio.pdb"), path.join(dst_dir, "studio.pdb"))
	end
}

function defaultConfigurations()
	configuration "Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "DEBUG" }
		flags { "Symbols", "WinMain" }

	configuration "Release"
		targetdir(BINARY_DIR .. "Release")
		defines { "NDEBUG" }
		flags { "Optimize", "WinMain" }

	configuration "RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize", "WinMain" }
end

function useLua()
	links {"lua"}
	includedirs { "../external/lua/include" }

	configuration {"Debug", "x32" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win32/debug"}

	configuration {"Release", "x32" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win32/release"}

	configuration {"RelWithDebInfo", "x32" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win32/release"}

	configuration {"Debug", "x64" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win64/debug"}

	configuration {"Release", "x64" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win64/release"}

		configuration {"RelWithDebInfo", "x64" }
		libdirs {"../external/lua/lib/" .. ide_dir .. "/win64/release"}
end

function copyDlls(src_dir, platform_bit, platform_dir, dest_dir)
	local physx_suffix
	if platform_bit == 32 then
		configuration { "x32", dest_dir }
		physx_suffix = "x86"
	else
		configuration { "x64", dest_dir }
		physx_suffix = "x64"
	end

	postbuildcommands {
		"xcopy /Y \"$(SolutionDir)../../external/assimp/dll/" .. ide_dir .. "/" .. platform_dir .. "/" .. src_dir .. "\\assimp.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\nvToolsExt".. tostring(platform_bit) .. "_1.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CommonCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CookingCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CharacterKinematicCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\""
	}
end

solution "LumixEngine"
	configurations { "Debug", "Release", "RelWithDebInfo" }
	platforms { "x32", "x64" }
	flags { "FatalWarnings", "NoPCH" }
	includedirs {"../src", "../src/engine"}
	location "tmp"
	language "C++"
	startproject "studio"

project "engine"
	kind "SharedLib"

	files { "../src/engine/**.h", "../src/engine/**.cpp", "genie.lua" }
	excludes { "../src/engine/**/osx/*"}

	defines { "BUILDING_ENGINE" }

	defaultConfigurations()

project "physics"
	kind "SharedLib"

	files { "../src/physics/**.h", "../src/physics/**.cpp" }
	excludes { "../src/engine/**/osx/*"}
	includedirs { "../external/physx/include", "../external/bgfx/include" }
	defines { "BUILDING_PHYSICS" }
	links { "engine", "renderer", "studio_lib", "editor" }

	useLua()

	configuration { "x64" }
		libdirs {"../external/physx/lib/" .. ide_dir .. "/win64"}
		links {"PhysX3CHECKED_x64", "PhysX3CommonCHECKED_x64", "PhysX3CharacterKinematicCHECKED_x64", "PhysX3CookingCHECKED_x64" }
	configuration { "x32" }
		libdirs {"../external/physx/lib/" .. ide_dir .. "/win32"}
		links {"PhysX3CHECKED_x86", "PhysX3CommonCHECKED_x86", "PhysX3CharacterKinematicCHECKED_x86", "PhysX3CookingCHECKED_x86"}

	configuration { "Debug" }
		links { "PhysX3ExtensionsDEBUG", "PhysXVisualDebuggerSDKDEBUG" }
	configuration { "Release" }
		links { "PhysX3ExtensionsCHECKED", "PhysXVisualDebuggerSDKCHECKED" }
	configuration { "RelWithDebInfo" }
		links { "PhysX3ExtensionsCHECKED", "PhysXVisualDebuggerSDKCHECKED" }

	defaultConfigurations()


project "renderer"
	kind "SharedLib"

	files { "../src/renderer/**.h", "../src/renderer/**.cpp" }
	includedirs { "../external/bgfx/include" }
	defines { "BUILDING_RENDERER" }
	links { "engine", "psapi" }
	useLua()
	defaultConfigurations()

	configuration { "x64" }
		libdirs {"../external/bgfx/lib/" .. ide_dir .. "/win64"}

	configuration { "x32" }
		libdirs {"../external/bgfx/lib/" .. ide_dir .. "/win32"}

	configuration "Debug"
		links {"bgfxDebug"}

	configuration "Release"
		links {"bgfxRelease"}

	configuration "RelWithDebInfo"
		links {"bgfxRelease"}

project "animation"
	kind "SharedLib"

	files { "../src/animation/**.h", "../src/animation/**.cpp" }
	includedirs { "../src" }
	includedirs { "../external/bgfx/include" }
	defines { "BUILDING_ANIMATION" }
	links { "engine", "renderer" }

	defaultConfigurations()

project "editor"
	kind "SharedLib"

	files { "../src/editor/**.h", "../src/editor/**.cpp" }
	includedirs { "../src", "../src/editor", "../external/bgfx/include" }
	defines { "BUILDING_EDITOR" }
	links { "renderer", "engine" }

	defaultConfigurations()

project "audio"
	kind "SharedLib"

	files { "../src/audio/**.h", "../src/audio/**.cpp", "../src/audio/**.c" }
	includedirs { "../src", "../src/audio", "../external/bgfx/include" }
	defines { "BUILDING_AUDIO" }
	links { "engine", "dxguid", "studio_lib", "editor" }

	useLua()
	defaultConfigurations()

project "lua_script"
	kind "SharedLib"

	files { "../src/lua_script/**.h", "../src/lua_script/**.cpp" }
	includedirs { "../src", "../src/lua_script", "../external/lua/include", "../external/bgfx/include" }
	defines { "BUILDING_LUA_SCRIPT" }
	links { "studio_lib", "engine", "renderer", "editor" }

	useLua()
	defaultConfigurations()

project "unit_tests"
	kind "ConsoleApp"

	files { "../src/unit_tests/**.h", "../src/unit_tests/**.cpp" }
	includedirs { "../src", "../src/unit_tests", "../external/bgfx/include" }
	links { "engine", "animation", "renderer" }

	useLua()
	defaultConfigurations()


project "studio_lib"
	kind "SharedLib"

	files { "../src/studio_lib/**.h", "../src/studio_lib/**.inl", "../src/studio_lib/**.cpp" }
	includedirs { "../src", "../src/studio_lib", "../external/lua/include", "../external/bgfx/include", "../external/assimp/include", "../external/crunch/include" }
	links { "animation", "editor", "engine", "assimp", "crnlib", "winmm", "renderer" }
	defines { "BUILDING_STUDIO_LIB" }

	useLua()
	defaultConfigurations()

	copyDlls("Debug", 32, "win32", "Debug")
	copyDlls("Debug", 64, "win64", "Debug")
	copyDlls("Release", 32, "win32", "Release")
	copyDlls("Release", 64, "win64", "Release")
	copyDlls("Release", 32, "win32", "RelWithDebInfo")
	copyDlls("Release", 64, "win64", "RelWithDebInfo")

	configuration { "Debug", "x64" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win64/debug"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win64/debug"}
	configuration { "Debug", "x32" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win32/debug"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win32/debug"}
	configuration { "Release", "x64" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win64/release"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win64/release"}
	configuration { "Release", "x32" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win32/release"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win32/release"}
	configuration { "RelWithDebInfo", "x64" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win64/release"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win64/release"}
	configuration { "RelWithDebInfo", "x32" }
		libdirs {"../external/assimp/lib/" .. ide_dir .. "/win32/release"}
		libdirs {"../external/crunch/lib/" .. ide_dir .. "/win32/release"}


project "studio"
	kind "WindowedApp"

	debugdir "../../LumixEngine_data"

	files { "../src/studio/**.cpp" }
	includedirs { "../src", "../src/studio_lib", "../external/lua/include", "../external/bgfx/include", "../external/assimp/include", "../external/crunch/include" }
	links { "studio_lib", "animation", "editor", "engine", "renderer", "winmm" }

	useLua()
	defaultConfigurations()