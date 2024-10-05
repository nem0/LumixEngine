newoption {
	trigger = "plugins",
	description = "Add plugins to project, can be a comma-separated list, e.g. --plugins=pluginA,pluginB"
}

newoption {
	trigger = "dynamic-plugins",
	description = "Plugins are dynamic libraries."
}

newoption {
	trigger = "embed-resources",
	description = "Embed resources (shaders, ...) in executable"
}

newoption {
	trigger = "no-physics",
	description = "Do not build physics plugin."
}

newoption {
	trigger = "no-navigation",
	description = "Do not build navigation plugin."
}

newoption {
	trigger = "no-animation",
	description = "Do not build animation plugin."
}

newoption {
	trigger = "no-renderer",
	description = "Do not build renderer plugin."
}

newoption {
	trigger = "no-audio",
	description = "Do not build audio plugin."
}

newoption {
	trigger = "no-lua-script",
	description = "Do not build lua script plugin."
}

newoption {
	trigger = "no-gui",
	description = "Do not build ingame GUI plugin."
}

newoption {
	trigger = "with-app",
	description = "Do build app."
}

newoption {
	trigger = "with-basis-universal",
	description = "Use basis universal compression."
}

newoption {
	trigger = "with-game",
	description = "Build game plugin."
}

newoption {
	trigger = "working-dir",
	description = "Working directory."
}

newoption {
	trigger = "debug-args",
	description = "Arguments passed to Studio in debug mode."
}

newoption {
	trigger = "release-args",
	description = "Arguments passed to Studio in release mode."
}

newoption {
	trigger = "no-studio",
	description = "Do not build Studio."
}

newoption {
	trigger = "luau-dynamic",
	description = "Build luau as dynamic library. Only valid if Luau source code is available."
}

newoption {
	trigger = "gcc",
	value = "GCC",
	description = "Choose GCC flavor",
	allowed = {
		{ "linux-gcc", 			"Linux (GCC compiler)" 				},
		{ "linux-gcc-5", 		"Linux (GCC-5 compiler)"			},
		{ "linux-clang", 		"Linux (Clang compiler)"			},
		{ "windows-clang", 		"Windows (Clang compiler)"			},
	}
}

-- process _OPTIONS
build_studio = not _OPTIONS["no-studio"]
build_app = _OPTIONS["with-app"] or false
local embed_resources = _OPTIONS["embed-resources"]
local working_dir = _OPTIONS["working-dir"]
local debug_args = _OPTIONS["debug-args"]
local release_args = _OPTIONS["release-args"]
local luau_dynamic = _OPTIONS["luau-dynamic"]
local use_basisu =  _OPTIONS["with-basis-universal"]
local dynamic_plugins = _OPTIONS["dynamic-plugins"]
local build_luau = os.isdir("../external/_repos/luau")
local build_physx = os.isdir("../external/_repos/physx")

if luau_dynamic and not build_luau then
	printf("Luau source code not found, can't build Luau as dynamic library.")
end

local plugins = {}
local base_plugins = {}

if _OPTIONS["plugins"] then
	plugins = string.explode( _OPTIONS["plugins"], ",")
end

if _OPTIONS["no-lua-script"] == nil then
	table.insert(plugins, "lua_script")
	table.insert(base_plugins, "lua_script")
end

for	_, v in ipairs { "physics", "renderer", "audio", "gui", "animation", "navigation" } do
	if _OPTIONS["no-" .. v] == nil then
		table.insert(plugins, v)
		table.insert(base_plugins, v)
	end
end

if _ACTION == "gmake" or _ACTION == "ninja" then
	if "linux-gcc-5" == _OPTIONS["gcc"] then
		premake.gcc.cc  = "gcc-5"
		premake.gcc.cxx = "g++-5"
		premake.gcc.ar  = "ar"
		
	elseif "linux-clang" == _OPTIONS["gcc"] then
		premake.gcc.cc  = "clang"
		premake.gcc.cxx = "clang++"
		premake.gcc.ar  = "ar"

	elseif "windows-clang" == _OPTIONS["gcc"] then
		premake.gcc.cc  = "clang"
		premake.gcc.cxx = "clang++"
		premake.gcc.ar  = "llvm-ar"
	end
end

local LOCATION = "tmp/" .. _ACTION
local BINARY_DIR = LOCATION .. "/bin/"
local plugin_creators = {}
build_studio_callbacks = {}
build_app_callbacks = {}

-- TODO remove this once bindless branch is merged (plugins use this)
function useLua()
end

function hasPlugin(plugin)
	for _, v in ipairs(plugins) do
    if v == plugin then
      return true
    end
  end
  return false
end

function plugin(plugin_name)
	if not hasPlugin(plugin_name) then return false end

	if build_studio then
		project "studio"
			links(plugin_name)
	end

	if build_app then
		project "app"
			links {plugin_name}
	end
	project(plugin_name)
	libType()

	if build_studio then
		links { "editor" }
	end

	defaultConfigurations()
	return true
end

function linkPlugin(plugin_name)
	table.insert(plugin_creators, plugin_name)
	if build_studio then
		project "studio"
			links(plugin_name)
	end

	if build_app then
		project "app"
			links {plugin_name}
	end
end

function detect_plugins()
	local plugins_dirs = os.matchdirs("../plugins/*")
	for k, plugin_dir in ipairs(plugins_dirs) do
		local dir = string.sub(plugin_dir, 12, -1)
		table.insert(plugins, dir)
	end
end
detect_plugins()

function defaultConfigurations()
	configuration "Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "NDEBUG", "LUMIX_DEBUG" }
		flags { "Symbols", "ReleaseRuntime" }

	configuration "RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize" }

	configuration "linux"
		defines { "_GLIBCXX_USE_CXX11_ABI=0" }
		links { "pthread" }

	configuration { "vs20*"}
		defines { "_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS" }
		buildoptions { "/wd4503 /wd4251"}
		
	configuration {}
		files {
			"../src/lumix.natvis",
			".editorconfig"
		}
		defines { "_ITERATOR_DEBUG_LEVEL=0", "STBI_NO_STDIO" }
		flags { "FullSymbols" } -- VS can't set brekpoints from time to time, only rebuilding several times or using FullSymbols helps
end

function linkLib(lib)
	links {lib}

	local use_prebuilt = not os.isdir("../external/_repos/" .. lib)
	if use_prebuilt then
		for platform,target_platform in pairs({win="windows", linux="linux", }) do
			configuration { "x64", target_platform }
				libdirs { "../external/" .. lib .. "/lib/" .. platform }
				libdirs { "../external/" .. lib .. "/dll/" .. platform }
		end
		configuration {}
	end
end

function libType()
	if dynamic_plugins then
		kind "SharedLib"
	else
		kind "StaticLib"
	end
end

function linkPhysX()
	if hasPlugin("physics") then
		defines { "PX_PHYSX_STATIC_LIB", "PX_PHYSX_CHARACTER_STATIC_LIB" }
		linkLib "PhysX"
	end
end

solution "LumixEngine"
	flags { "Cpp20" }
	configurations { "Debug", "RelWithDebInfo" }
	platforms { "x64" }
	flags { 
		"UseObjectResponseFile",
		"UseLDResponseFile",
		"LinkSupportCircularDependencies",
		"FatalWarnings", 
		"NoPCH", 
		"NoExceptions", 
		"NoRTTI", 
		"NoEditAndContinue"
	}
	includedirs {"../src", "../external" }
	location(LOCATION)
	language "C++"
	startproject "studio"

	if not dynamic_plugins then
		defines {"STATIC_PLUGINS"}
	end

	if not build_studio then
		removefiles { "../src/**/editor/*" }
	end

	if "windows-clang" == _OPTIONS["gcc"] then
		removeflags { "LinkSupportCircularDependencies"}
		buildoptions { 
			"-Wno-return-type-c-linkage" -- PxGetFoundation
		}
	end


	configuration { "linux" }
		buildoptions {
			"-m64",
			"-fPIC",
			"-no-canonical-prefixes",
			"-Wa,--noexecstack",
			"-fstack-protector",
			"-ffunction-sections",
			"-Wunused-value",
			"-Wundef",
			"-msse2",
			"-Wno-multichar",
			"-Wno-undef",
			"-Wno-ignored-attributes"
		}
		
		if "linux-clang" ~= _OPTIONS["gcc"] then
			buildoptions { 
				"-Wno-psabi"
			}
		else
			buildoptions { 
				"-Wno-logical-op-parentheses",
				"-Wno-dynamic-class-memaccess",
				"-Wno-unused-value",
				"-Wno-switch"
			}
		end
		
		linkoptions {
			"-Wl,--gc-sections",
			"-fopenmp"
		}

	configuration { "vs*" }
		defines { "_HAS_EXCEPTIONS=0" }
		buildoptions { "/Zc:char8_t-" }

	configuration { "vs*", "RelWithDebInfo" }
		flags { "NoBufferSecurityCheck" }
		buildoptions { "/GL", "/Oi" }
		linkoptions { "/LTCG:incremental"}

	configuration "not macosx"
		removefiles { "../src/**/osx/*"}
		
	configuration "not linux"
		removefiles { "../src/**/linux/*"}
		
	configuration "not windows"
		removefiles { "../src/**/win/*"}

project "core"
	libType()
	defaultConfigurations()
	defines { "BUILDING_CORE" }

	files { "../src/core/**.h",
			"../src/core/**.c",
			"../src/core/**.cpp",
			"../src/core/**.inl",
			"genie.lua",
			"../external/wyhash/**.*"
	}

	configuration { "linux" }
		buildoptions { "`pkg-config --cflags gtk+-3.0`" }

project "engine"
	libType()
	defines { "BUILDING_ENGINE" }
	links { "core" }
	defaultConfigurations()
	includedirs { "../src", "../external/freetype/include" }

	if dynamic_plugins then
		linkLib "freetype"
		defines { "LZ4_DLL_EXPORT" }
	end

	files { "../src/engine/**.h",
			"../src/engine/**.c",
			"../src/engine/**.cpp",
			"../src/engine/**.inl",
			"genie.lua",
			"../external/imgui/**.h",
			"../external/imgui/**.cpp",
			"../external/imgui/**.inl",
			"../external/lz4/**.c",
			"../external/lz4/**.h",
	}
	excludes {
		"../external/imgui/imgui_demo.cpp",
		"../external/imgui/imgui.cpp",
		"../external/imgui/imgui_tables.cpp",
		"../external/imgui/imgui_draw.cpp",
		"../external/imgui/imgui_widgets.cpp",
		"../external/imgui/imgui_freetype.cpp",
	}

	configuration { "linux" }
		buildoptions { "`pkg-config --cflags gtk+-3.0`" }

if plugin "physics" then
	files { "../src/physics/**.h", "../src/physics/**.cpp" }
	includedirs { "../external/physx/include/" }
	defines { "BUILDING_PHYSICS", "LUMIX_STATIC_PHYSX" }
	links { "core", "engine", "renderer" }
	linkPhysX()
end

if plugin "renderer" then
	files {
		"../src/renderer/**.h",
		"../src/renderer/**.cpp",
		"../src/renderer/**.c",
	}
	files { "../data/shaders/**.*" }
	excludes { 
		"../external/meshoptimizer/clusterizer.cpp",
		"../external/meshoptimizer/overdrawanalyzer.cpp",
		"../external/meshoptimizer/overdrawoptimizer.cpp",
		"../external/meshoptimizer/spatialorder.cpp",
		"../external/meshoptimizer/stripifier.cpp",
		"../external/meshoptimizer/vcacheanalyzer.cpp",
		"../external/meshoptimizer/vcacheoptimizer.cpp",
		"../external/meshoptimizer/vertexcodec.cpp",
		"../external/meshoptimizer/vertexfilter.cpp",
		"../external/meshoptimizer/vfetchanalyzer.cpp",
		"../external/meshoptimizer/vfetchoptimizer.cpp",
		"../src/renderer/editor/voxelizer_ui.cpp",
	}
	
	if build_studio then
		files {
			"../external/meshoptimizer/**.*",
			"../external/mikktspace/**.*",
			"../external/openfbx/**.*",
		}
	end

	if use_basisu then
		defines { "LUMIX_BASIS_UNIVERSAL" }
		includedirs { "../external/basisu/include" }
	end
	includedirs { "../src", "../external/freetype/include", "../external/", "../external/dx12/", "../external/pix/include/WinPixEventRuntime" }
	
	defines { "BUILDING_RENDERER" }
	libdirs { "../external/pix/bin/x64" }
	links { "core", "engine" }

	if build_studio and use_basisu then
		linkLib "basisu"
	end

	linkLib "freetype"

	configuration { "linux" }
		links { "GL", "X11", "Xi" }
	
	configuration { "windows" }
		links { "psapi", "dxguid" }
end
		
if plugin "animation" then
	files { "../src/animation/**.h", "../src/animation/**.cpp" }
	includedirs { "../src" }
	defines { "BUILDING_ANIMATION" }
	links { "core", "engine", "renderer" }
end

if plugin "audio" then
	files { 
		"../src/audio/**.h",
		"../src/audio/**.cpp",
		"../external/stb/stb_vorbis.cpp"
	}

	includedirs { "../src", "../src/audio" }
	defines { "BUILDING_AUDIO" }
	links { "core", "engine" }

	configuration "windows"
		links { "dxguid" }
end

if plugin "navigation" then
	files {
		"../src/navigation/**.h",
		"../src/navigation/**.cpp",
		"../external/recast/src/**.cpp",
		"../external/recast/include/**.h"
	}

	-- use unity build for recast
	excludes {
		"../external/recast/src/DetourAlloc.cpp",
		"../external/recast/src/DetourAssert.cpp",
		"../external/recast/src/DetourCommon.cpp",
		"../external/recast/src/DetourCrowd.cpp",
		"../external/recast/src/DetourLocalBoundary.cpp",
		"../external/recast/src/DetourNavMesh.cpp",
		"../external/recast/src/DetourNavMeshBuilder.cpp",
		"../external/recast/src/DetourNavMeshQuery.cpp",
		"../external/recast/src/DetourNode.cpp",
		"../external/recast/src/DetourObstacleAvoidance.cpp",
		"../external/recast/src/DetourPathCorridor.cpp",
		"../external/recast/src/DetourPathQueue.cpp",
		"../external/recast/src/DetourProximityGrid.cpp",
		"../external/recast/src/Recast.cpp",
		"../external/recast/src/RecastAlloc.cpp",
		"../external/recast/src/RecastArea.cpp",
		"../external/recast/src/RecastAssert.cpp",
		"../external/recast/src/RecastFilter.cpp",
		"../external/recast/src/RecastLayers.cpp",
		"../external/recast/src/RecastMesh.cpp",
		"../external/recast/src/RecastRasterization.cpp",
		"../external/recast/src/RecastRegion.cpp",
		-- don't include following files in unity build because they have conflicting symbols with RecastMesh.cpp and each other
		--"../external/recast/src/RecastContour.cpp",
		--"../external/recast/src/RecastMeshDetail.cpp",
	}

	includedirs { "../src", "../src/navigation", "../external/recast/include" }
	links { "core", "engine", "renderer" }
end

if plugin "gui" then
	files { "../src/gui/**.h", "../src/gui/**.cpp" }
	includedirs { "../src", "../src/gui" }
	links { "core", "engine", "renderer" }
	defines { "BUILDING_GUI" }
	
	configuration { "vs*" }
		links { "winmm", "psapi" }
end
	
if plugin "lua_script" then
	if build_luau and not luau_dynamic then
		defines { "LUMIX_STATIC_LUAU" }
	end

	configuration { "vs20*" }
		libdirs {  "../external/luau/lib/win" }
		links "Luau"
		
	configuration { "linux" }
		libdirs {  "../external/luau/lib/linux" }
		links "Luau"

	configuration {}

	files { "../src/lua_script/**.h", "../src/lua_script/**.cpp" }
	includedirs { "../external/luau/include"
		, "../src"
		, "../src/lua_script"
	}
	defines { "BUILDING_LUA_SCRIPT" }
	links { "core", "engine" }

	if hasPlugin "renderer" then
		links { "renderer" }
	end

	if build_luau and build_studio then
		project "studio"
			links {"Luau"}
	end
	if build_luau and build_app then
		project "app"
			links {"Luau"}
	end
end

if _OPTIONS["with-game"] ~= nil then
	dofile("../../" .. _OPTIONS["with-game"] .. "/genie.lua")
end

for _, plugin in ipairs(plugins) do
	local path = "../../" .. plugin .. "/genie.lua";
	if os.isfile(path) then
		dofile(path)
	else
		 path = "../plugins/" .. plugin .. "/genie.lua";
		if os.isfile(path) then
			dofile(path)
		end
	end
end

function dbgHelp()
	configuration { "windows" }
		files { "../external/dbghelp/**.dll" }	
		copy { "../external/dbghelp/**.dll" }
	configuration {}
end

if build_app then
	project "app"
		kind "ConsoleApp"
		defaultConfigurations()
		includedirs { "../src", "../src/app" }
		files { "../src/app/main.cpp" }

		if working_dir then
			debugdir ("../../" .. working_dir)
		else 
			debugdir "../data"
		end
		
		if #plugins > 0 and dynamic_plugins then
			local def = ""
			for idx, plugin in ipairs(plugins) do
				if idx > 1 then 
					def = def .. ",";
				end
				def = def .. "\"" .. plugin .. "\""
			end
			defines { "LUMIXENGINE_PLUGINS=" .. def }
		end

		if not dynamic_plugins then	
			if hasPlugin "lua_script" then linkLib "Luau" end
			if hasPlugin "physics" then
				linkPhysX()
			end
			links { "core", "engine" }
			if _OPTIONS["no-lua-script"] == nil then
				links { "lua_script" }
			end

			if use_basisu then
				linkLib "basisu"
			end
			linkLib "freetype"
			
			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }

			configuration {}
		else
			links { "core", "engine" }
		end
		
		if build_studio then
			links { "editor" }
		end
		if build_studio and use_basisu then
			linkLib "basisu"
		end
		
		configuration { "windows" }
			kind "WindowedApp"
			libdirs { "../external/pix/bin/x64" }

		configuration { "linux" }
			links { "GL", "X11", "dl", "rt", "Xi" }
		
		configuration {"vs*"}
			links { "winmm", "imm32", "version" }
		configuration {}

		for _, callback in ipairs(build_app_callbacks) do
			callback()
		end
end

if build_studio then
	project "editor"
		libType()
		defaultConfigurations()
		defines { "BUILDING_EDITOR" }
		links { "core", "engine" }

		files {
			"../src/editor/**.h",
			"../src/editor/**.cpp"
		}
		includedirs {
			"../src",
			"../src/editor",
			"../external"
		}
		
		if #plugins > 0 and dynamic_plugins then
			local def = ""
			for idx, plugin in ipairs(plugins) do
				if idx > 1 then 
					def = def .. ",";
				end
				def = def .. "\"" .. plugin .. "\""
			end
			defines { "LUMIXENGINE_PLUGINS=" .. def }
		end

		configuration { "windows" }
			links { "winmm" }

		if dynamic_plugins then	
			configuration {"vs*"}
				links { "imm32", "version" }
			configuration {}
		end

	project "studio"
		kind "WindowedApp"
		files { "../src/studio/**.cpp" }
		dbgHelp()
		includedirs { "../src" }
		defaultConfigurations()
		links { "editor", "core", "engine", "renderer" }

		if embed_resources then
			files { "../src/studio/**.rc" }
		end

		if debug_args then
			configuration { "Debug" }
				debugargs { debug_args }
			configuration {}
		end

		if release_args then
			configuration { "RelWithDebInfo" }
				debugargs { release_args }
			configuration {}
		end
		
		if working_dir then
			debugdir ("../../" .. working_dir)
		else
			debugdir "../data"
		end

		if not dynamic_plugins then	
			linkLib "freetype"
			if use_basisu then linkLib "basisu" end
			if hasPlugin "physics" then linkPhysX() end
			if hasPlugin "lua_script" then linkLib "Luau" end

			configuration { "linux" }
				links { "dl", "GL", "X11", "rt", "Xi" }
				if _ACTION == "gmake" then
					linkoptions { "-Wl,-rpath '-Wl,$$ORIGIN'" }
				end

			configuration { "windows" }
				links { "psapi", "dxguid", "winmm" }

			configuration { "vs*" }
				libdirs { "../external/pix/bin/x64" }
				files { "../external/pix/bin/x64/WinPixEventRuntime.dll" }
				copy { "../external/pix/bin/x64/WinPixEventRuntime.dll" }
			
			configuration {}
		end

		for _, callback in ipairs(build_studio_callbacks) do
			callback()
		end
		
		if not build_luau or luau_dynamic then
			configuration { "windows" }
				files { "../external/luau/lib/win/Luau.dll" }
				copy { "../external/luau/lib/win/Luau.dll" }
		end

		configuration { "linux" }
			links {"gtk-3", "gobject-2.0"}

		configuration {"windows"}
			links { "winmm", "imm32", "version", "shell32", "gdi32", "comdlg32", "advapi32", "ole32" }
end

if build_physx and hasPlugin("physics") then
	printf("Using PhysX from external/_repos/physx (build from source code)")
	project "PhysX"
		kind "StaticLib"
		files { "../external/_repos/physx/physx/source/**.cpp", "../external/_repos/physx/physx/include/**.h", "../external/_repos/physx/pxshared/**.h" }
		removefiles { "../**/unix/*", "../**/linux/*" }
		includedirs { "../external/_repos/physx/physx/include"
			, "../external/_repos/physx/pxshared/include"
			
			, "../external/_repos/physx/physx/source/common/include"
			, "../external/_repos/physx/physx/source/fastxml/include"
			, "../external/_repos/physx/physx/source/filebuf/include"
			, "../external/_repos/physx/physx/source/foundation/include"
			, "../external/_repos/physx/physx/source/geomutils/include"
			, "../external/_repos/physx/physx/source/lowlevel/api/include"
			, "../external/_repos/physx/physx/source/lowlevel/common/include"
			, "../external/_repos/physx/physx/source/lowlevel/common/include/collision"
			, "../external/_repos/physx/physx/source/lowlevel/common/include/pipeline"
			, "../external/_repos/physx/physx/source/lowlevel/common/include/utils"
			, "../external/_repos/physx/physx/source/lowlevel/software/include"
			, "../external/_repos/physx/physx/source/lowlevelaabb/include"
			, "../external/_repos/physx/physx/source/lowleveldynamics/include"
			, "../external/_repos/physx/physx/source/physx/src"
			, "../external/_repos/physx/physx/source/physx/src/buffering"
			, "../external/_repos/physx/physx/source/physx/src/device"
			, "../external/_repos/physx/physx/source/physx/src/gpu"
			, "../external/_repos/physx/physx/source/physx/src/windows"
			, "../external/_repos/physx/physx/source/physxgpu/include"
			, "../external/_repos/physx/physx/source/physxmetadata/core/include"
			, "../external/_repos/physx/physx/source/physxmetadata/extensions/include"
			, "../external/_repos/physx/physx/source/physxvehicle/src/physxmetadata/include"
			, "../external/_repos/physx/physx/source/pvd/include"
			, "../external/_repos/physx/physx/source/scenequery/include"
			, "../external/_repos/physx/physx/source/simulationcontroller/include"
			
			, "../external/_repos/physx/physx/source/physxcooking/src/"
			, "../external/_repos/physx/physx/source/physxcooking/src/convex"
			, "../external/_repos/physx/physx/source/physxcooking/src/mesh"
			
			, "../external/_repos/physx/physx/source/physxextensions/src"
			, "../external/_repos/physx/physx/source/physxextensions/src/serialization"
			, "../external/_repos/physx/physx/source/physxextensions/src/serialization/Binary"
			, "../external/_repos/physx/physx/source/physxextensions/src/serialization/File"
			, "../external/_repos/physx/physx/source/physxextensions/src/serialization/Xml"
			
			, "../external/_repos/physx/physx/source/physxvehicle/src"
			
			, "../external/_repos/physx/physx/source/simulationcontroller/src"
			
			, "../external/_repos/physx/physx/source/foundation/src"
			, "../external/_repos/physx/physx/source/common/src"
			, "../external/_repos/physx/physx/source/fastxml/src"
			, "../external/_repos/physx/physx/source/geomutils/src"

			, "../external/_repos/physx/physx/source/geomutils/src/**"
		}
		defines { "NDEBUG", "PX_PHYSX_STATIC_LIB", "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS", "PX_COOKING" }
		flags { "OptimizeSize", "ReleaseRuntime" }

		configuration { "windows" }
			targetdir "../external/physx/lib/win"
		
		configuration { "vs20*" }
			buildoptions { "/wd5055"}
end

if os.isdir("../external/_repos/freetype") then
	printf("Using FreeType from external/_repos/freetype (build from source code)")
	project "freetype"
		kind "StaticLib"
		files {
			"../external/_repos/freetype/src/autofit/autofit.c",
			"../external/_repos/freetype/src/base/ftbase.c",
			"../external/_repos/freetype/src/base/ftbbox.c",
			"../external/_repos/freetype/src/base/ftbdf.c",
			"../external/_repos/freetype/src/base/ftbitmap.c",
			"../external/_repos/freetype/src/base/ftcid.c",
			"../external/_repos/freetype/src/base/ftfstype.c",
			"../external/_repos/freetype/src/base/ftgasp.c",
			"../external/_repos/freetype/src/base/ftglyph.c",
			"../external/_repos/freetype/src/base/ftgxval.c",
			"../external/_repos/freetype/src/base/ftinit.c",
			"../external/_repos/freetype/src/base/ftmm.c",
			"../external/_repos/freetype/src/base/ftotval.c",
			"../external/_repos/freetype/src/base/ftpatent.c",
			"../external/_repos/freetype/src/base/ftpfr.c",
			"../external/_repos/freetype/src/base/ftstroke.c",
			"../external/_repos/freetype/src/base/ftsynth.c",
			"../external/_repos/freetype/src/base/ftsystem.c",
			"../external/_repos/freetype/src/base/fttype1.c",
			"../external/_repos/freetype/src/base/ftwinfnt.c",
			"../external/_repos/freetype/src/bdf/bdf.c",
			"../external/_repos/freetype/src/cache/ftcache.c",
			"../external/_repos/freetype/src/cff/cff.c",
			"../external/_repos/freetype/src/cid/type1cid.c",
			"../external/_repos/freetype/src/gzip/ftgzip.c",
			"../external/_repos/freetype/src/lzw/ftlzw.c",
			"../external/_repos/freetype/src/pcf/pcf.c",
			"../external/_repos/freetype/src/pfr/pfr.c",
			"../external/_repos/freetype/src/psaux/psaux.c",
			"../external/_repos/freetype/src/pshinter/pshinter.c",
			"../external/_repos/freetype/src/psnames/psmodule.c",
			"../external/_repos/freetype/src/raster/raster.c",
			"../external/_repos/freetype/src/sfnt/sfnt.c",
			"../external/_repos/freetype/src/smooth/smooth.c",
			"../external/_repos/freetype/src/truetype/truetype.c",
			"../external/_repos/freetype/src/type1/type1.c",
			"../external/_repos/freetype/src/type42/type42.c",
			"../external/_repos/freetype/src/winfonts/winfnt.c",
			"../external/_repos/freetype/builds/windows/ftdebug.c"
		}
		includedirs { "../external/_repos/freetype/include" }
		defines { "NDEBUG", "FT2_BUILD_LIBRARY", "_CRT_SECURE_NO_WARNINGS" }
		flags { "ReleaseRuntime", "MinimumWarnings" }
		targetname "freetype"
		
		-- TODO release/debug target dirs
		configuration { "linux" }
			targetdir "../external/freetype/lib/linux"
		
		configuration { "windows" }
			targetdir "../external/freetype/lib/win"

		configuration { "vs20*" }
			buildoptions { "/wd4312"}

		defaultConfigurations()
else
	printf("Using FreeType from external/freetype (prebuilt)")
end

if build_luau then
	printf("Using Luau from external/_repos/luau (build from source code)")
	project "Luau"
		if luau_dynamic then
			kind "SharedLib"
		else
			kind "StaticLib"
		end
		files { "../external/_repos/luau/Ast/src/**.cpp"
			, "../external/_repos/luau/Ast/src/**.h"
			, "../external/_repos/luau/CodeGen/src/**.cpp"
			, "../external/_repos/luau/CodeGen/src/**.h"
			, "../external/_repos/luau/Compiler/src/**.cpp"
			, "../external/_repos/luau/Compiler/src/**.h"
			, "../external/_repos/luau/VM/src/**.cpp"
			, "../external/_repos/luau/VM/src/**.h"
		}

		if not luau_dynamic then
			files { "../external/_repos/luau/Analysis/src/**.cpp"
				, "../external/_repos/luau/Analysis/src/**.h"
				, "../external/_repos/luau/Config/src/**.cpp"
				, "../external/_repos/luau/Config/src/**.h"
			}

			includedirs { "../external/_repos/luau/Analysis/include/" 
				, "../external/_repos/luau/Config/include/"
			}
		end

		includedirs { "../external/_repos/luau/Ast/include/"
			, "../external/_repos/luau/CodeGen/include/"
			, "../external/_repos/luau/Common/include/"
			, "../external/_repos/luau/Compiler/include/"
			, "../external/_repos/luau/VM/include/"
			, "../external/_repos/luau/VM/src/"
		}

		removeflags { "NoExceptions", "NoRTTI" }
		flags { "OptimizeSize", "ReleaseRuntime" }

		configuration { "linux"}
			targetdir "../external/luau/lib/linux"

		configuration { "windows" }
			targetdir "../external/luau/lib/win"
			defines {
				"_CRT_SECURE_NO_WARNINGS",
				"LUA_API=__declspec(dllexport)",
				"LUACODE_API=__declspec(dllexport)"
			}
		configuration {}

	if not luau_dynamic then
		solution "LumixEngine"
			configuration { "vs20*" }
			defines { "LUMIX_LUAU_ANALYSIS" }
			configuration {}
	end
else	
	printf("Using Luau from external/luau (prebuilt)")
end

for _, plugin in ipairs(base_plugins) do
	linkPlugin(plugin)
end

-- write plugins.inl
local file = io.open("../src/engine/plugins.inl", "w")
io.output(file)
io.write("// generated by genie.lua\n\n")
io.write "#ifdef LUMIX_PLUGIN_DECLS\n"
	for _, plugin in ipairs(plugin_creators) do
		io.write([[extern "C" ISystem* createPlugin_]] .. plugin .. "(Engine&);\n")
	end
io.write "#elif defined LUMIX_EDITOR_PLUGINS_DECLS\n"
	if not dynamic_plugins then
		for _, plugin in ipairs(plugin_creators) do
			io.write([[extern "C" Lumix::StudioApp::IPlugin* setStudioApp_]] .. plugin .. "(StudioApp&);\n")
		end
	end
io.write "#elif defined LUMIX_EDITOR_PLUGINS\n"
	if not dynamic_plugins then
		for _, plugin in ipairs(plugin_creators) do
			io.write "{\n"
			io.write("\tStudioApp::IPlugin* plugin = setStudioApp_" .. plugin .. "(*this);\n")
			io.write("\tif (plugin) this->addPlugin(*plugin);\n")
			io.write "}\n"
		end
	end
io.write "#elif defined LUMIX_PLUGINS_STRINGS\n"
	if dynamic_plugins then
		for _, plugin in ipairs(plugin_creators) do
			io.write("\"" .. plugin .. "\", ")
		end
	end
	io.write("nullptr\n")
io.write "#else\n"
	if not dynamic_plugins then
		for _, plugin in ipairs(plugin_creators) do
			io.write "{\n"
			io.write("\tISystem* p = createPlugin_" .. plugin .. "(engine);\n")
			io.write "\tif (p) engine.getSystemManager().addSystem(p, nullptr);\n"
			io.write "}\n"
		end
	end
io.write "#endif\n"
io.close(file)