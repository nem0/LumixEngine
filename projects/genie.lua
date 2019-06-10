local ide_dir = iif(_ACTION == nil, "vs2019", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end
local binary_api_dir = iif(ide_dir == "vs2019", "vs2017", ide_dir)

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local build_app = true
local build_studio = true
local build_game = false
local working_dir = nil
local debug_args = nil
local release_args = nil
local plugins = {}
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


newoption {
	trigger = "plugins",
	description = "Add plugins to project, can be a comma-separated list, e.g. --plugins=pluginA,pluginB"
}

newoption {
	trigger = "static-plugins",
	description = "Plugins are static libraries."
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
	trigger = "no-app",
	description = "Do not build app."
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

if _OPTIONS["with-game"] then
	build_game = _OPTIONS["with-game"]
	table.insert(plugins, build_game)
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
end

if _OPTIONS["no-renderer"] == nil then
	table.insert(plugins, "renderer")
end

if _OPTIONS["no-audio"] == nil then
	table.insert(plugins, "audio")
end

if _OPTIONS["no-lua-script"] == nil then
	table.insert(plugins, "lua_script")
end

if _OPTIONS["no-gui"] == nil or _ACTION ~= "vs2019" then
	table.insert(plugins, "gui")
end

if _OPTIONS["no-animation"] == nil then
	table.insert(plugins, "animation")
end

if _OPTIONS["no-navigation"] == nil then
	table.insert(plugins, "navigation")
end

if _OPTIONS["no-studio"] then
	build_studio = false
end


if _OPTIONS["no-app"] then
	build_app = false
end

newoption {
		trigger = "gcc",
		value = "GCC",
		description = "Choose GCC flavor",
		allowed = {
			{ "android-x86",    	"Android - x86"            	 		},
			{ "linux-gcc", 			"Linux (GCC compiler)" 				},
			{ "linux-gcc-5", 		"Linux (GCC-5 compiler)"			},
			{ "linux-clang", 		"Linux (Clang compiler)"			}
		}
	}

newaction {
	trigger = "run-render-tests",
	description = "Run render tests",
	
	execute = function() 
		os.execute([[cd ..\..\lumixengine_data\ && bin\app.exe -window -pipeline_define RENDER_TEST -script unit_tests/render_tests/main.lua]])
	end
}
	
newaction {
	trigger = "update-tests",
	description = "Update render & editor tests, use when serializaiton changes",
	
	execute = function() 
		local editor_tests = { "all_components", "basic", "copy_paste_delete" }
		os.execute([[cd ..\..\lumixengine_data\ && bin\studio.exe -run_script unit_tests\editor\update_tests.lua -pseudorandom_guid]])
		for _, test in ipairs(editor_tests) do
			os.execute([[rmdir /S /Q "..\..\lumixengine_data\unit_tests\editor\]] .. test .. [["]])
			os.execute([[del /Q "..\..\lumixengine_data\unit_tests\editor\]] .. test .. [[.unv"]])
			os.execute([[xcopy /I /Y /E "..\..\lumixengine_data\universes\]] .. test ..[[" "..\..\lumixengine_data\unit_tests\editor\]] .. test .. [["]])
			os.execute([[rmdir /S /Q "..\..\lumixengine_data\universes\]] .. test .. [["]])
			os.execute([[del /Q "..\..\lumixengine_data\universes\]] .. test .. [[.unv"]])
		end
		
		local render_tests = { "direct_light", "indirect_light", "text_mesh" }
		for _, test in ipairs(render_tests) do
			os.execute([[xcopy /I /Y /E "..\..\lumixengine_data\unit_tests\render_tests\]] .. test ..[[" "..\..\lumixengine_data\universes\]] .. test .. [["]])
			os.execute([[cd ..\..\lumixengine_data\ && bin\studio.exe -run_script unit_tests\render_tests\update_tests.lua -open ]] .. test)
			os.execute([[cd ..\..\lumixengine_data\ && copy /Y universes\]] .. test .. [[.unv  unit_tests\render_tests\]] .. test .. [[.unv]])
			os.execute([[rmdir /S /Q "..\..\lumixengine_data\universes\]] .. test .. [["]])
			os.execute([[del /Q "..\..\lumixengine_data\universes\]] .. test .. [[.unv"]])
		end
	end
}
	
newaction {
	trigger = "install",
	description = "Install in ../../LumixEngine_data/bin",
	execute = function()
		local src_dir = "tmp/vs209/bin/RelWithDebInfo/"
		local dst_dir = "../../LumixEngine_data/bin/"
		
		function installDll(filename)
			os.copyfile(path.join(src_dir, filename .. ".dll"), path.join(dst_dir, filename .. ".dll"))
		end

		function installDllWithPdb(filename)
			installDll(filename)
			os.copyfile(path.join(src_dir, filename .. ".pdb"), path.join(dst_dir, filename .. ".pdb"))
		end
		
		--installDllWithPdb "animation"
		--installDllWithPdb "audio"
		--installDllWithPdb "editor"
		--installDllWithPdb "engine"
		--installDllWithPdb "lua_script"
		--installDllWithPdb "physics"
		--installDllWithPdb "renderer"
		
		installDll "PhysX3CommonCHECKED_x64"
		installDll "PhysX3CookingCHECKED_x64"
		installDll "PhysX3CharacterKinematicCHECKED_x64"
		installDll "PhysX3CHECKED_x64"
		installDll "nvToolsExt64_1"

		os.copyfile(path.join(src_dir, "studio.exe"), path.join(dst_dir, "studio.exe"))
		os.copyfile(path.join(src_dir, "studio.pdb"), path.join(dst_dir, "studio.pdb"))
		os.copyfile(path.join(src_dir, "app.exe"), path.join(dst_dir, "app.exe"))
		os.copyfile(path.join(src_dir, "app.pdb"), path.join(dst_dir, "app.pdb"))
	end
}

		
function defaultConfigurations()
	configuration "Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "DEBUG", "_DEBUG" }
		flags { "Symbols", "WinMain" }

	configuration "RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize", "WinMain" }

	configuration "linux"
		buildoptions { "-std=c++14" }
		defines { "_GLIBCXX_USE_CXX11_ABI=0" }
		links { "pthread" }

	configuration { "vs20*"}
		buildoptions { "/wd4503"}
		
	configuration {}
		files { "lumix.natvis", "../.editorconfig" }

end

function linkLib(lib)
	links {lib}

	for conf,conf_dir in pairs({Debug="debug", RelWithDebInfo="release"}) do
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
	if _OPTIONS["static-plugins"] then
		linkLib "luajit"
	else
		configuration { "windows" }
			defines { "LUA_BUILD_AS_DLL" }
		configuration {}
	end
	includedirs { "../external/luajit/include" }
end

function copyDlls(src_dir, platform_dir, dest_dir)
	local physx_suffix
	configuration { "x64", dest_dir, "windows" }
	physx_suffix = "64"

	postbuildcommands {
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXCommon_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXCooking_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysXFoundation_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. binary_api_dir .. "/" .. platform_dir .. "\\" .. src_dir .. "\\PhysX_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		[[xcopy /Y "$(SolutionDir)..\..\..\external\dbghelp\dbghelp.dll" "$(SolutionDir)bin\]] .. dest_dir .. "\"",
		[[xcopy /Y "$(SolutionDir)..\..\..\external\dbghelp\dbgcore.dll" "$(SolutionDir)bin\]] .. dest_dir .. "\""
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
	if has_plugin("physics") then
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
			libdirs {"../external/physx/lib/" .. binary_api_dir .. "/win64/debug"}

		configuration { "RelWithDebInfo" }
			libdirs {"../external/physx/lib/" .. binary_api_dir .. "/win64/release"}

		configuration {}
			defines {"PX_PHYSX_CHARACTER_STATIC_LIB"}
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
		configuration { "android-*" }
			flags {
				"NoImportLib",
			}
			includedirs {
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/include",
				"$(ANDROID_NDK_ROOT)/sources/android/native_app_glue",
			}
			linkoptions {
				"-nostdlib",
				"-static-libgcc",
			}
			links {
				"c",
				"dl",
				"m",
				"android",
				"log",
				"gnustl_static",
				"gcc",
			}
			buildoptions {
				"-fPIC",
				"-no-canonical-prefixes",
				"-Wa,--noexecstack",
				"-fstack-protector",
				"-ffunction-sections",
				"-Wno-psabi",
				"-Wunused-value",
				"-Wundef",
			}
			buildoptions_cpp {
				"-std=c++14",
			}
			linkoptions {
				"-no-canonical-prefixes",
				"-Wl,--no-undefined",
				"-Wl,-z,noexecstack",
				"-Wl,-z,relro",
				"-Wl,-z,now",
			}
		
		configuration { "android-x86" }
			androidPlatform = "android-24"
			libdirs {
				path.join(_libDir, "lib/android-x86"),
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/libs/x86",
			}
			includedirs {
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/libs/x86/include",
			}
			buildoptions {
				"--sysroot=" .. path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86"),
				"-march=i686",
				"-mtune=atom",
				"-mstackrealign",
				"-msse3",
				"-mfpmath=sse",
				"-Wunused-value",
				"-Wundef",
			}
			linkoptions {
				"--sysroot=" .. path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86"),
				path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86/usr/lib/crtbegin_so.o"),
				path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86/usr/lib/crtend_so.o"),
			}
	
		configuration {}	
	
		
		if "android-x86" == _OPTIONS["gcc"] then
			if not os.getenv("ANDROID_NDK_X86") or not os.getenv("ANDROID_NDK_ROOT") then
				print("Set ANDROID_NDK_X86 and ANDROID_NDK_ROOT envrionment variables.")
			end

			premake.gcc.cc  = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-gcc\""
			premake.gcc.cxx = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-g++\""
			premake.gcc.ar  = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-ar\""
			LOCATION = "tmp/android-x86_gmake"
		
		elseif "linux-gcc" == _OPTIONS["gcc"] then
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

	configuration { "linux-*" }
		buildoptions {
			"-fPIC",
			"-no-canonical-prefixes",
			"-Wa,--noexecstack",
			"-fstack-protector",
			"-ffunction-sections",
			"-Wno-psabi",
			"-Wunused-value",
			"-Wundef",
			"-msse2",
		}
		linkoptions {
			"-Wl,--gc-sections",
		}
	
	configuration { "linux-*", "x32" }
		buildoptions {
			"-m32",
		}

	configuration { "linux-*", "x64" }
		buildoptions {
			"-m64",
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

	configuration "android-*"
		removefiles { "../src/**/win/*"}
		
	configuration "not asmjs" 
		removefiles { "../src/**/asmjs/*"}
	
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

	if embed_resources then
		removefiles { "../src/engine/no_resources.cpp" }
	else
		removefiles { "../src/engine/resources.cpp" }
	end

	defines { "BUILDING_ENGINE" }
	includedirs { "../external/luajit/include" }
	
	linkLib "luajit"

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
		includedirs { "../src", "../external/cmft/include", "../external/crnlib/include" }
		defines { "BUILDING_RENDERER" }
		links { "engine" }

		if build_studio then
			links { "editor" }
			linkLib "crnlib"
			linkLib "cmft"
		end
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
		if build_game then
			debugdir ("../../" .. build_game)
		elseif working_dir then
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
			for _, plugin in ipairs(plugins) do
				forceLink ("s_" .. plugin .. "_plugin_register")
				links { plugin }
				if build_studio then
					forceLink("setStudioApp_" .. plugin)
				end
			end
					
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
			links {"engine"}
			
			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }

			configuration {}
		else
			links { "editor", "engine" }
		end
		if build_studio then
			linkLib "crnlib"
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
		
		if build_game then
			debugdir ("../../" .. build_game)
		elseif working_dir then
			debugdir ("../../" .. working_dir)
		else
			debugdir "../data"
		end


		files { "../src/studio/**.cpp" }
		includedirs { "../src" }

		if _OPTIONS["static-plugins"] then	
			for _, plugin in ipairs(plugins) do
				forceLink("s_" .. plugin .. "_plugin_register")
				forceLink("setStudioApp_" .. plugin)
				links { plugin }
			end
			
		

			links { "editor", "engine" }
			linkLib "crnlib"
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
			copyDlls("Debug", "win64", "Debug")
			copyDlls("Release", "win64", "RelWithDebInfo")
		end
		
		if "linux-gcc" == _OPTIONS["gcc"] then
			copyDlls("debug", "linux64", "Debug")
			copyDlls("release", "linux64", "Release")
		end
end
