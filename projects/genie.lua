local ide_dir = iif(_ACTION == nil, "vs2013", _ACTION)
local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"

newoption {
	trigger = "static-plugins",
	description = "Plugins are static libraries.",
}

newaction {
	trigger = "install",
	description = "Install in ../../LumixEngine_data/bin",
	execute = function()
		local src_dir = "tmp/vs2013/bin/RelWithDebInfo/"
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
		installDllWithPdb "assimp"
		
		installDll "PhysX3CommonCHECKED_x64"
		installDll "PhysX3CookingCHECKED_x64"
		installDll "PhysX3CharacterKinematicCHECKED_x64"
		installDll "PhysX3CHECKED_x64"
		installDll "nvToolsExt64_1"

		os.copyfile(path.join(src_dir, "studio.exe"), path.join(dst_dir, "studio.exe"))
		os.copyfile(path.join(src_dir, "studio.pdb"), path.join(dst_dir, "studio.pdb"))
	end
}

newaction {
	trigger = "install32",
	description = "Install 32bit in ../../LumixEngine_data/bin32",
	execute = function()
		local src_dir = "tmp/vs2013/bin/RelWithDebInfo/"
		local dst_dir = "../../LumixEngine_data/bin32/"
		
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
		installDllWithPdb "assimp"
		
		installDll "PhysX3CommonCHECKED_x86"
		installDll "PhysX3CookingCHECKED_x86"
		installDll "PhysX3CharacterKinematicCHECKED_x86"
		installDll "PhysX3CHECKED_x86"
		installDll "nvToolsExt32_1"

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
		
	configuration {}
		files { "lumix.natvis" }
	
end

function linkLib(lib)
	links {lib}

	configuration { "x64", "Debug" }
		libdirs {"../external/" .. lib .. "/lib/win64_" .. ide_dir .. "/debug"}

	configuration { "x64", "Release" }
		libdirs {"../external/" .. lib .. "/lib/win64_" .. ide_dir .. "/release"}

	configuration { "x64", "RelWithDebInfo" }
		libdirs {"../external/" .. lib .. "/lib/win64_" .. ide_dir .. "/release"}

	configuration { "x32", "Debug" }
		libdirs {"../external/" .. lib .. "/lib/win32_" .. ide_dir .. "/debug"}

	configuration { "x32", "Release" }
		libdirs {"../external/" .. lib .. "/lib/win32_" .. ide_dir .. "/release"}

	configuration { "x32", "RelWithDebInfo" }
		libdirs {"../external/" .. lib .. "/lib/win32_" .. ide_dir .. "/release"}

	configuration {}
end

function useLua()
	if _OPTIONS["static-plugins"] then
		linkLib("lua")
	else		
		defines { "LUA_BUILD_AS_DLL" }
	end
	includedirs { "../external/lua/include" }
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
		"xcopy /Y \"$(SolutionDir)../../../external/assimp/dll/" .. platform_dir .. "_" .. ide_dir .. "/" .. src_dir .. "\\assimp.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\nvToolsExt".. tostring(platform_bit) .. "_1.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CommonCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CookingCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CharacterKinematicCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\""
	}
end

function libType()
	if _OPTIONS["static-plugins"] then
		kind "StaticLib"
	else
		kind "SharedLib"
	end
end

function linkPhysX()
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
		
	configuration {}
end

function forceLink(name)
	configuration { "x64" }
		linkoptions {"/INCLUDE:" .. name}
	configuration { "x32" }
		linkoptions {"/INCLUDE:_" .. name}
	configuration {}
end

solution "LumixEngine"
	configurations { "Debug", "Release", "RelWithDebInfo" }
	platforms { "x32", "x64" }
	flags { "FatalWarnings", "NoPCH" }
	includedirs {"../src", "../src/engine"}
	location(LOCATION)
	language "C++"
	startproject "studio"
	if _OPTIONS["static-plugins"] then
		defines {"STATIC_PLUGINS"}
	end
	
project "engine"
	libType()

	files { "../src/engine/**.h", "../src/engine/**.cpp", "genie.lua" }
	excludes { "../src/engine/**/osx/*"}

	defines { "BUILDING_ENGINE" }
	includedirs { "../external/lua/include" }
	if not _OPTIONS["static-plugins"] then
		linkoptions {"/DEF:\"../../../src/engine/engine.def\""}
	end

	linkLib("lua")

	defaultConfigurations()

project "physics"
	libType()

	files { "../src/physics/**.h", "../src/physics/**.cpp" }
	excludes { "../src/engine/**/osx/*"}
	includedirs { "../external/physx/include/" .. ide_dir, "../external/bgfx/include" }
	defines { "BUILDING_PHYSICS" }
	links { "engine", "renderer", "editor" }

	useLua()

	linkPhysX()

	defaultConfigurations()


project "renderer"
	libType()

	files { "../src/renderer/**.h", "../src/renderer/**.cpp" }
	includedirs { "../src", "../external/bgfx/include" }
	defines { "BUILDING_RENDERER" }
	links { "engine", "psapi", "editor" }
	useLua()

	linkLib("bgfx")
	
	defaultConfigurations()

project "animation"
	libType()

	files { "../src/animation/**.h", "../src/animation/**.cpp" }
	includedirs { "../src" }
	includedirs { "../external/bgfx/include" }
	defines { "BUILDING_ANIMATION" }
	links { "engine", "renderer", "editor" }

	defaultConfigurations()

project "audio"
	libType()

	files { "../src/audio/**.h", "../src/audio/**.cpp", "../src/audio/**.c" }
	includedirs { "../src", "../src/audio", "../external/bgfx/include" }
	defines { "BUILDING_AUDIO" }
	links { "engine", "dxguid", "editor" }

	useLua()
	defaultConfigurations()
	
project "navigation"
	libType()

	files { "../src/navigation/**.h", "../src/navigation/**.cpp" }
	includedirs { "../src", "../src/navigation", "../external/recast/include" }
	includedirs { "../external/bgfx/include" }
	links { "engine", "editor", "renderer" }
	linkLib "recast"
	
	useLua()
	defaultConfigurations()
	
project "lua_script"
	libType()

	files { "../src/lua_script/**.h", "../src/lua_script/**.cpp" }
	includedirs { "../src", "../src/lua_script", "../external/lua/include", "../external/bgfx/include" }
	defines { "BUILDING_LUA_SCRIPT" }
	links { "editor", "engine", "renderer" }

	useLua()
	defaultConfigurations()

project "unit_tests"
	kind "ConsoleApp"

	files { "../src/unit_tests/**.h", "../src/unit_tests/**.cpp" }
	includedirs { "../src", "../src/unit_tests", "../external/bgfx/include" }
	links { "engine", "animation", "renderer" }
	if _OPTIONS["static-plugins"] then	
		links { "engine", "winmm", "psapi" }
		linkLib("bgfx")
	end

	useLua()
	defaultConfigurations()

project "render_test"
	kind "WindowedApp"

	debugdir "../../LumixEngine_data"

	files { "../src/render_test/**.h", "../src/render_test/**.cpp" }
	includedirs { "../src", "../src/render_test", "../external/bgfx/include" }
	if _OPTIONS["static-plugins"] then	
		forceLink("s_animation_plugin_register")
		forceLink("s_audio_plugin_register")
		forceLink("s_lua_script_plugin_register")
		forceLink("s_navigation_plugin_register")
		forceLink("s_physics_plugin_register")
		forceLink("s_renderer_plugin_register")

		forceLink("setStudioApp_animation")
		forceLink("setStudioApp_audio")
		forceLink("setStudioApp_lua_script")
		forceLink("setStudioApp_physics")
		forceLink("setStudioApp_renderer")

		links { "engine", "winmm", "audio", "animation", "renderer", "lua_script", "navigation", "physics", "psapi", "dxguid" }
		linkLib("crnlib")
		linkLib("assimp")
		linkLib("bgfx")
		linkLib("lua")
		linkLib("recast")
		linkPhysX()
	end
	links { "engine", "animation", "renderer" }

	
	useLua()
	defaultConfigurations()


project "editor"
	libType()

	files { "../src/editor/**.h", "../src/editor/**.cpp", "../src/editor/**.inl" }
	includedirs { "../src", "../src/editor", "../external/bgfx/include" }
	defines { "BUILDING_EDITOR" }
	links { "engine", "winmm" }
	linkLib("crnlib")
	linkLib("assimp")
	includedirs { "../src", "../external/lua/include", "../external/bgfx/include", "../external/assimp/include", "../external/crnlib/include" }

	useLua()
	defaultConfigurations()

	copyDlls("Debug", 32, "win32", "Debug")
	copyDlls("Debug", 64, "win64", "Debug")
	copyDlls("Release", 32, "win32", "Release")
	copyDlls("Release", 64, "win64", "Release")
	copyDlls("Release", 32, "win32", "RelWithDebInfo")
	copyDlls("Release", 64, "win64", "RelWithDebInfo")

project "studio"
	kind "WindowedApp"

	debugdir "../../LumixEngine_data"

	files { "../src/studio/**.cpp" }
	includedirs { "../src" }

	if _OPTIONS["static-plugins"] then	
		forceLink("s_animation_plugin_register")
		forceLink("s_audio_plugin_register")
		forceLink("s_lua_script_plugin_register")
		forceLink("s_navigation_plugin_register")
		forceLink("s_physics_plugin_register")
		forceLink("s_renderer_plugin_register")

		forceLink("setStudioApp_animation")
		forceLink("setStudioApp_audio")
		forceLink("setStudioApp_lua_script")
		forceLink("setStudioApp_physics")
		forceLink("setStudioApp_renderer")

		links { "engine", "winmm", "audio", "animation", "renderer", "lua_script", "navigation", "physics", "psapi", "dxguid" }
		linkLib("crnlib")
		linkLib("assimp")
		linkLib("bgfx")
		linkLib("lua")
		linkLib("recast")
		linkPhysX()
	end
	links { "editor" }

	useLua()
	defaultConfigurations()