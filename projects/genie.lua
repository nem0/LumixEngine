local ide_dir = iif(_ACTION == nil, "vs2019", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end
binary_api_dir = iif(ide_dir == "vs2019", "vs2017", ide_dir)

local ROOT_DIR = path.getabsolute("../")
local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
build_app = false
build_studio = true
local working_dir = nil
local debug_args = nil
local release_args = nil
local plugins = {}
local base_plugins = {}
local embed_resources = false
build_studio_callbacks = {}
build_app_callbacks = {}

function has_plugin(plugin)
	for _, v in ipairs(plugins) do
    if v == plugin then
      return true
    end
  end
  return false
end


function linkPlugin(plugin_name)
	if build_studio then
		project "studio"
			if _OPTIONS["static-plugins"] then	
				forceLink("s_" .. plugin_name .. "_plugin_register")
				forceLink("setStudioApp_" .. plugin_name)
			end
			links(plugin_name)
	end

	if build_app then
		project "app"
		links {plugin_name}
		if _OPTIONS["static-plugins"] then	
			forceLink ("s_" .. plugin_name .. "_plugin_register")
		end
		if build_studio then
			forceLink("setStudioApp_" .. plugin_name)
		end
	end
end

newoption {
	trigger = "plugins",
	description = "Add plugins to project, can be a comma-separated list, e.g. --plugins=pluginA,pluginB"
}

newoption {
	trigger = "static-plugins",
	description = "Plugins are static libraries."
}

newoption {
	trigger = "static-physx",
	description = "Linked physx static lib."
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
	trigger = "with-unit-tests",
	description = "Build unit tests."
}

newoption {
	trigger = "with-app",
	description = "Do build app."
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

if _OPTIONS["plugins"] then
	plugins = string.explode( _OPTIONS["plugins"], ",")
end

if _OPTIONS["embed-resources"] then
	embed_resources = true
end

if _OPTIONS["working-dir"] then
	working_dir = _OPTIONS["working-dir"]
end

if _OPTIONS["debug-args"] then
	debug_args = _OPTIONS["debug-args"]
end

if _OPTIONS["release-args"] then
	release_args = _OPTIONS["release-args"]
end

if not _OPTIONS["no-physics"] then
	table.insert(plugins, "physics")
	table.insert(base_plugins, "physics")
end

if _OPTIONS["no-renderer"] == nil then
	table.insert(plugins, "renderer")
	table.insert(base_plugins, "renderer")
end

if _OPTIONS["no-audio"] == nil then
	table.insert(plugins, "audio")
	table.insert(base_plugins, "audio")
end

if _OPTIONS["no-lua-script"] == nil then
	table.insert(plugins, "lua_script")
	table.insert(base_plugins, "lua_script")
end

if _OPTIONS["no-gui"] == nil then
	table.insert(plugins, "gui")
	table.insert(base_plugins, "gui")
end

if _OPTIONS["no-animation"] == nil then
	table.insert(plugins, "animation")
	table.insert(base_plugins, "animation")
end

if _OPTIONS["no-navigation"] == nil then
	table.insert(plugins, "navigation")
	table.insert(base_plugins, "navigation")
end

if _OPTIONS["no-studio"] then
	build_studio = false
end


if _OPTIONS["with-app"] then
	build_app = true
end

function detect_plugins()
	local f = io.popen([[if exist ..\plugins dir /B ..\plugins]])
	if not f then return end
	for line in f:lines() do 
		table.insert(plugins, line)
	end
	f:close()
end
detect_plugins()

newoption {
		trigger = "gcc",
		value = "GCC",
		description = "Choose GCC flavor",
		allowed = {
			{ "linux-gcc", 			"Linux (GCC compiler)" 				},
			{ "linux-gcc-5", 		"Linux (GCC-5 compiler)"			},
			{ "linux-clang", 		"Linux (Clang compiler)"			}
		}
	}

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
		buildoptions { "/wd4503"}
		
	configuration {}
		files { "lumix.natvis", "../.editorconfig" }
		defines { "_ITERATOR_DEBUG_LEVEL=0" }
end

function linkLib(lib)
	links {lib}

	for conf,conf_dir in pairs({Debug="release", RelWithDebInfo="release"}) do
		for platform,target_platform in pairs({win="windows", linux="linux", }) do
			configuration { "x64", conf, target_platform }
				libdirs {"../external/" .. lib .. "/lib/" .. platform .. "64" .. "_" .. binary_api_dir .. "/" .. conf_dir}
				libdirs {"../external/" .. lib .. "/dll/" .. platform .. "64" .. "_" .. binary_api_dir .. "/" .. conf_dir}
		end
	end
	configuration {}
end

function useLua()
	links {"lua51"}
	linkLib "luajit"
	includedirs { path.join(ROOT_DIR, "external/luajit/include") }
end

function copyDlls(src_dir, platform_dir, dest_dir)
	local physx_suffix
	configuration { "x64", dest_dir, "windows" }
	physx_suffix = "64"

	if _OPTIONS["static-physx"] == nil then
		postbuildcommands {
			"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXCommon_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
			"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXCooking_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
			"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXFoundation_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
			"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysX_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
			[[xcopy /Y "$(SolutionDir)..\..\..\external\dbghelp\dbghelp.dll" "$(SolutionDir)bin\]] .. dest_dir .. "\"",
			[[xcopy /Y "$(SolutionDir)..\..\..\external\dbghelp\dbgcore.dll" "$(SolutionDir)bin\]] .. dest_dir .. "\""
		}
	end
end

function libType()
	if _OPTIONS["static-plugins"] then
		kind "StaticLib"
	else
		kind "SharedLib"
	end
end

function linkPhysX()
	if has_plugin("physics") then
		if _OPTIONS["static-physx"] then
			configuration { "x64", "vs20*" }
				links { 
					"PhysXCharacterKinematic_static_64",
					"PhysXCommon_static_64",
					"PhysXCooking_static_64",
					"PhysXExtensions_static_64",
					"PhysXFoundation_static_64",
					"PhysXPvdSDK_static_64",
					"PhysXVehicle_static_64",
					"PhysX_static_64"
				}

			configuration {}
				libdirs {"../external/physx/lib/" .. binary_api_dir .. "/win64/release_static"}
				defines {
					"PX_PHYSX_CHARACTER_STATIC_LIB",
					"PX_PHYSX_STATIC_LIB"
				}
		else 
			configuration { "x64", "vs20*" }
				links { 
					"FastXml_static_64",
					"LowLevel_static_64",
					"LowLevelAABB_static_64",
					"LowLevelDynamics_static_64",
					"PhysX_64",
					"PhysXCommon_64",
					"PhysXCooking_64",
					"PhysXExtensions_static_64",
					"PhysXFoundation_64",
					"PhysXCharacterKinematic_static_64",
					"PhysXPvdSDK_static_64",
					"PhysXTask_static_64",
					"PhysXVehicle_static_64",
					"SceneQuery_static_64",
					"SimulationController_static_64"
				}

			configuration { "Debug" }
				libdirs {"../external/physx/lib/" .. binary_api_dir .. "/win64/release"}

			configuration { "RelWithDebInfo" }
				libdirs {"../external/physx/lib/" .. binary_api_dir .. "/win64/release"}

			configuration {}
				defines {"PX_PHYSX_CHARACTER_STATIC_LIB"}
		end
	end
end

function forceLink(name)

	configuration { "linux-*" }
		linkoptions {"-u " .. name}
	configuration { "x64", "vs*" }
		linkoptions {"/INCLUDE:" .. name}
	configuration {}
end

solution "LumixEngine"
	flags { "Cpp17" }
	if _ACTION == "gmake" then
		if "linux-gcc" == _OPTIONS["gcc"] then
			LOCATION = "tmp/gcc"

		elseif "linux-gcc-5" == _OPTIONS["gcc"] then
			premake.gcc.cc  = "gcc-5"
			premake.gcc.cxx = "g++-5"
			premake.gcc.ar  = "ar"
			LOCATION = "tmp/gcc5"
			
		elseif "linux-clang" == _OPTIONS["gcc"] then
			premake.gcc.cc  = "clang"
			premake.gcc.cxx = "clang++"
			premake.gcc.ar  = "ar"
			LOCATION = "tmp/clang"

		end
		BINARY_DIR = LOCATION .. "/bin/"
	end
	
	if not build_studio then
		removefiles { "../src/**/editor/*" }
	end

	configuration { "linux" }
		buildoptions {
			"-m64",
			"-fPIC",
			"-no-canonical-prefixes",
			"-Wa,--noexecstack",
			"-fstack-protector",
			"-ffunction-sections",
			"-Wno-psabi",
			"-Wunused-value",
			"-Wundef",
			"-msse2",
			"-Wno-multichar",
			"-Wno-undef",
		}
		linkoptions {
			"-Wl,--gc-sections",
		}

	configuration {}
	
	configurations { "Debug", "RelWithDebInfo" }
	platforms { "x64" }
	flags { 
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

	configuration { "vs*" }
		defines { "_HAS_EXCEPTIONS=0" }

	configuration "not macosx"
		removefiles { "../src/**/osx/*"}
		
	configuration "not linux"
		removefiles { "../src/**/linux/*"}
		
	configuration "not windows"
		removefiles { "../src/**/win/*"}

	configuration {}

	if _OPTIONS["static-plugins"] then
		defines {"STATIC_PLUGINS"}
	end

project "engine"
	libType()

	files { "../src/engine/**.h",
			"../src/engine/**.cpp",
			"genie.lua",
			"../external/imgui/**.h",
			"../external/imgui/**.cpp",
			"../external/imgui/**.inl"
	}

	defines { "BUILDING_ENGINE" }
	includedirs { "../external/luajit/include", "../external/freetype/include" }
	
	linkLib "lua51"
	linkLib "luajit"
	if not _OPTIONS["static-plugins"] then
		linkLib "freetype"
	end

	configuration { "vs20*" }
		if not _OPTIONS["static-plugins"] then
			linkoptions {"/DEF:\"../../../src/engine/engine.def\""}
		end


	defaultConfigurations()

if has_plugin("physics") then
	project "physics"
		libType()

		files { "../src/physics/**.h", "../src/physics/**.cpp" }

		includedirs { "../external/physx/include/" }
		defines { "BUILDING_PHYSICS" }
		links { "engine", "editor", "renderer" }
		useLua()
		linkPhysX()

		defaultConfigurations()
end


if has_plugin("renderer") then
	project "renderer"
		libType()

		files { "../src/renderer/**.h", "../src/renderer/**.cpp", "../src/renderer/**.c"}
		files { "../data/pipelines/**.*"}
		includedirs { "../src", "../external/cmft/include", "../external/nvtt/include", "../external/freetype/include" }
		defines { "BUILDING_RENDERER" }
		links { "engine" }

		if build_studio then
			links { "editor" }
			linkLib "nvtt"
			linkLib "cmft"
		end
		linkLib "freetype"
		links { "opengl32" }
		configuration { "linux-*" }
			links { "GL", "X11" }
		configuration {}
		useLua()
		
		configuration { "windows" }
			links { "psapi" }

		defaultConfigurations()
end
		
if has_plugin("animation") then
	project "animation"
		libType()

		files { "../src/animation/**.h", "../src/animation/**.cpp" }
		includedirs { "../src" }
		defines { "BUILDING_ANIMATION" }
		links { "engine", "renderer" }

		if build_studio then
			links { "editor" }
		end
		
		useLua()
		defaultConfigurations()
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
	
if has_plugin("audio") then
	project "audio"
		libType()

		files { 
			"../src/audio/**.h",
			"../src/audio/**.cpp",
			"../external/stb/stb_vorbis.cpp"
		}
		includedirs { "../src", "../src/audio" }
		defines { "BUILDING_AUDIO" }
		links { "engine" }

		if build_studio then
			links { "editor" }
		end

		configuration "windows"
			links { "dxguid" }
		configuration {}

		useLua()
		defaultConfigurations()
end
	
if has_plugin("navigation") then
	project "navigation"
		libType()

		files { "../src/navigation/**.h", "../src/navigation/**.cpp", "../external/recast/src/**.cpp" }
		includedirs { "../src", "../src/navigation", "../external/recast/include" }
		links { "engine", "renderer" }
		linkLib "recast"
		
		if build_studio then
			links { "editor" }
		end
		
		useLua()
		defaultConfigurations()
end

if has_plugin("gui") then
	project "gui"
		libType()

		files { "../src/gui/**.h", "../src/gui/**.cpp" }
		includedirs { "../src", "../src/gui" }
		links { "engine", "renderer" }
		
		defines { "BUILDING_GUI" }
		
		configuration { "vs*" }
			links { "winmm", "psapi" }
		configuration {}

		if build_studio then
			links { "editor" }
		end
	
		useLua()
		defaultConfigurations()
end
	
if has_plugin("lua_script") then
	project "lua_script"
		libType()

		files { "../src/lua_script/**.h", "../src/lua_script/**.cpp" }
		includedirs { "../src", "../src/lua_script" }
		defines { "BUILDING_LUA_SCRIPT" }
		links { "engine", "renderer" }

		if build_studio then
			links { "editor" }
		end

		useLua()
		defaultConfigurations()
end

if build_app then
	project "app"
		if working_dir then
			debugdir ("../../" .. working_dir)
		else 
			debugdir "../data"
		end

		kind "ConsoleApp"
		
		if #plugins > 0 then
			local def = ""
			for idx, plugin in ipairs(plugins) do
				if idx > 1 then 
					def = def .. ",";
				end
				def = def .. "\"" .. plugin .. "\""
			end
			defines { "LUMIXENGINE_PLUGINS=" .. def }
		end

		includedirs { "../src", "../src/app" }
		if _OPTIONS["static-plugins"] then	
			if build_studio then
				for _, plugin in ipairs(plugins) do
					forceLink("setStudioApp_" .. plugin)
				end
			end
				
			if has_plugin("renderer") then
				links { "opengl32" }
			end
			if has_plugin("physics") then
				linkPhysX()
			end
			if build_studio then links {"editor"} end

			links { "editor", "engine" }
			linkLib "nvtt"
			linkLib "freetype"
			linkLib "cmft"
			linkLib "luajit"
			linkLib "recast"
			
			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }

			configuration {}
		else
			links { "editor", "engine" }
		end
		if build_studio then
			linkLib "nvtt"
			linkLib "cmft"
		end
		
		linkLib "luajit"
		linkLib "recast"
		files { "../src/app/main.cpp" }

		configuration { "windows" }
			kind "WindowedApp"

		configuration { "linux-*" }
			links { "GL", "X11", "dl", "rt" }
		
		configuration {}
		
		configuration {"vs*"}
			links { "winmm", "imm32", "version" }
		configuration {}

		for _, callback in ipairs(build_app_callbacks) do
			callback()
		end
		
		useLua()
		defaultConfigurations()
end

for _, plugin in ipairs(base_plugins) do
	linkPlugin(plugin)
end


if build_studio then
	project "editor"
		libType()

		files {
			"../src/editor/**.h",
			"../src/editor/**.cpp"
		}
		defines { "BUILDING_EDITOR" }
		links { "engine" }
		includedirs {
			"../src",
			"../src/editor",
			"../external"
		}
		
		if #plugins > 0 then
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

		configuration {}

		if not _OPTIONS["static-plugins"] then	
			configuration {"vs*"}
				links { "winmm", "imm32", "version" }
			configuration {}
		end
		
		useLua()
		defaultConfigurations()

	project "studio"
		kind "WindowedApp"

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

		files { "../src/studio/**.cpp" }

		if embed_resources then
			files { "../src/studio/**.rc" }
		end

		includedirs { "../src" }

		if _OPTIONS["static-plugins"] then	
			links { "editor", "engine" }
			linkLib "nvtt"
			linkLib "freetype"
			linkLib "cmft"
			linkLib "luajit"
			linkLib "recast"
			
			if has_plugin("renderer") then
				links { "opengl32" }
			end
			if has_plugin "physics" then
				linkPhysX()
			end
			
			configuration { "linux-*" }
				links { "GL", "X11", "dl", "rt" }
				linkoptions { "-Wl,-rpath '-Wl,$$ORIGIN'" }

			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }
			
			configuration {}
		else
			links { "renderer", "editor", "engine" }
		end

		for _, callback in ipairs(build_studio_callbacks) do
			callback()
		end
		
		configuration {"vs*"}
			links { "winmm", "imm32", "version" }
		configuration {}
		
		useLua()
		defaultConfigurations()
		
		if _ACTION == "vs2019" then
			copyDlls("Release", "win64", "Debug")
			copyDlls("Release", "win64", "RelWithDebInfo")
		end
		
		if "linux-gcc" == _OPTIONS["gcc"] then
			copyDlls("release", "linux64", "Debug")
			copyDlls("release", "linux64", "Release")
		end
end
