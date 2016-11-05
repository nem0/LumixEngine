local ide_dir = iif(_ACTION == nil, "vs2015", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local build_physics = true
local build_unit_tests = true
local build_app = true
local build_studio = true
local build_gui = _ACTION == "vs2015"
local build_steam = false
local build_game = false

newoption {
	trigger = "static-plugins",
	description = "Plugins are static libraries."
}

newoption {
	trigger = "no-physics",
	description = "Do not build physics plugin."
}

newoption {
	trigger = "with-steam",
	description = "Build Steam plugin."
}

newoption {
	trigger = "no-gui",
	description = "Do not build ingame GUI plugin."
}

newoption {
	trigger = "no-unit-tests",
	description = "Do not build unit tests."
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
	trigger = "no-studio",
	description = "Do not build Studio."
}

if _OPTIONS["with-steam"] then
	build_steam = true
end

if _OPTIONS["with-game"] then
	build_game = _OPTIONS["with-game"]
end

if _OPTIONS["no-physics"] then
	build_physics = false
end


if _OPTIONS["no-gui"] then
	build_gui = false
end


if _OPTIONS["no-studio"] then
	build_studio = false
end

if _OPTIONS["no-unit-tests"] then
	build_unit_tests = false
end

if _OPTIONS["no-app"] then
	build_app = false
end

newoption {
		trigger = "gcc",
		value = "GCC",
		description = "Choose GCC flavor",
		allowed = {
			{ "asmjs",          	"Emscripten/asm.js"       	 		},
			{ "android-x86",    	"Android - x86"            	 		},
			{ "linux-gcc", 			"Linux (GCC compiler)" 				},
			{ "linux-gcc-5", 		"Linux (GCC-5 compiler)"			},
			{ "linux-clang", 		"Linux (Clang compiler)"			}
		}
	}
	
newaction {
	trigger = "install",
	description = "Install in ../../LumixEngine_data/bin",
	execute = function()
		local src_dir = "tmp/vs2015/bin/RelWithDebInfo/"
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
		installDllWithPdb "assimp"
		
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


function strip()
	configuration { "asmjs" }
		postbuildcommands {
			"$(SILENT) echo Running asmjs finalize.",
			"$(SILENT) \"$(EMSCRIPTEN)/emcc\" -O2 "
--				.. "-s EMTERPRETIFY=1 "
--				.. "-s EMTERPRETIFY_ASYNC=1 "
				.. "-s TOTAL_MEMORY=268435456 "
--				.. "-s ALLOW_MEMORY_GROWTH=1 "
--				.. "-s USE_WEBGL2=1 "
				.. "--memory-init-file 1 "
				.. "\"$(TARGET)\" -o \"$(TARGET)\".html "
--				.. "--preload-file ../../../examples/runtime@/"
		}
		
	configuration { "android-x86", "Release" }
		postbuildcommands {
			"$(SILENT) echo Stripping symbols.",
			"$(SILENT) $(ANDROID_NDK_X86)/bin/i686-linux-android-strip -s \"$(TARGET)\""
		}

	configuration {}
end

		
function defaultConfigurations()
	configuration "Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "DEBUG", "_DEBUG" }
		flags { "Symbols", "WinMain" }

	configuration "Release"
		targetdir(BINARY_DIR .. "Release")
		defines { "NDEBUG" }
		flags { "Optimize", "WinMain" }

	configuration "RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize", "WinMain" }

	configuration "linux"
		buildoptions { "-std=c++11" }
		links { "pthread" }

	configuration { "asmjs" }
		buildoptions { "-std=c++11" }
		
	configuration {}
		files { "lumix.natvis" }

end

function linkLib(lib)
	links {lib}

	for conf,conf_dir in pairs({Debug="debug", Release="release", RelWithDebInfo="release"}) do
		for platform,target_platform in pairs({win="windows", linux="linux", }) do
			configuration { "x64", conf, target_platform }
				libdirs {"../external/" .. lib .. "/lib/" .. platform .. "64" .. "_" .. ide_dir .. "/" .. conf_dir}
				libdirs {"../external/" .. lib .. "/dll/" .. platform .. "64" .. "_" .. ide_dir .. "/" .. conf_dir}
		end
	end
	for conf,conf_dir in pairs({Debug="debug", Release="release", RelWithDebInfo="release"}) do
		configuration { "android-x86", conf }
			libdirs {"../external/" .. lib .. "/lib/android-x86_gmake/" .. conf_dir}
	end
	configuration {}
end

function useLua()
	if _OPTIONS["static-plugins"] then
		linkLib("lua")
	else
		configuration { "windows", "not asmjs" }
			defines { "LUA_BUILD_AS_DLL" }
		configuration {}
	end
	includedirs { "../external/lua/include" }
end

function copyDlls(src_dir, platform_dir, dest_dir)
	local physx_suffix
	configuration { "x64", dest_dir, "windows" }
	physx_suffix = "x64"

	postbuildcommands {
		"xcopy /Y \"$(SolutionDir)../../../external/assimp/dll/" .. platform_dir .. "_" .. ide_dir .. "/" .. src_dir .. "\\assimp.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\nvToolsExt64_1.dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CommonCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CookingCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CharacterKinematicCHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\"",
		"xcopy /Y \"$(SolutionDir)../../../external/physx/dll/" .. ide_dir .. "/" .. platform_dir .. "\\PhysX3CHECKED_".. physx_suffix .. ".dll\" \"$(SolutionDir)bin/" .. dest_dir .. "\""
	}

	configuration { "linux-*" }
		postbuildcommands {
			"cp ../../../external/assimp/dll/" .. platform_dir .. "_" .. ide_dir .. "/" .. src_dir .. "/libassimp.so bin/" .. dest_dir,
			"cp ../../../external/physx/dll/linux64_gcc5/libPhysX3CommonCHECKED_".. physx_suffix .. ".so bin/" .. dest_dir,
			"cp ../../../external/physx/dll/linux64_gcc5/libPhysX3CookingCHECKED_".. physx_suffix .. ".so  bin/" .. dest_dir,
			"cp ../../../external/physx/dll/linux64_gcc5/libPhysX3CharacterKinematicCHECKED_".. physx_suffix .. ".so  bin/" .. dest_dir,
			"cp ../../../external/physx/dll/linux64_gcc5/libPhysX3CHECKED_".. physx_suffix .. ".so  bin/" .. dest_dir
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
	if build_physics then
		configuration { "x64", "vs20*" }
			libdirs {"../external/physx/lib/" .. ide_dir .. "/win64"}
			links {"PhysX3CHECKED_x64", "PhysX3CommonCHECKED_x64", "PhysX3CharacterKinematicCHECKED_x64", "PhysX3CookingCHECKED_x64" }
		configuration { "x64", "linux-*" }
			libdirs {"../external/physx/lib/linux64_gcc5", "../external/physx/dll/linux64_gcc5"}
			links {"PhysX3CHECKED_x64", "PhysX3CommonCHECKED_x64", "PhysX3CharacterKinematicCHECKED_x64", "PhysX3CookingCHECKED_x64" }
		

		configuration { "Debug" }
			links { "PhysX3ExtensionsDEBUG", "PhysXVisualDebuggerSDKDEBUG" }
		configuration { "Release" }
			links { "PhysX3ExtensionsCHECKED", "PhysXVisualDebuggerSDKCHECKED" }
		configuration { "RelWithDebInfo" }
			links { "PhysX3ExtensionsCHECKED", "PhysXVisualDebuggerSDKCHECKED" }
			
		configuration {}
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
				"-std=c++11",
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
	
		if "asmjs" == _OPTIONS["gcc"] then
			if not os.getenv("EMSCRIPTEN") then
				print("Set EMSCRIPTEN enviroment variable.")
			end
			premake.gcc.cc   = "\"$(EMSCRIPTEN)/emcc\""
			premake.gcc.cxx  = "\"$(EMSCRIPTEN)/em++\""
			premake.gcc.ar   = "\"$(EMSCRIPTEN)/emar\""
			_G["premake"].gcc.llvm = true
			premake.gcc.llvm = true
			LOCATION = "tmp/emscripten_gmake"
		
		elseif "android-x86" == _OPTIONS["gcc"] then
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
	
	configurations { "Debug", "Release", "RelWithDebInfo" }
	platforms { "x64" }
	flags { 
		"FatalWarnings", 
		"NoPCH", 
		"NoExceptions", 
		"NoRTTI", 
		"NoEditAndContinue"
	}
	includedirs {"../src", "../external", "../external/SDL/include" }
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

	configuration "asmjs"
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

	defines { "BUILDING_ENGINE" }
	includedirs { "../external/lua/include" }
	
	linkLib "lua"

	configuration { "vs20*" }
		if not _OPTIONS["static-plugins"] then
			linkoptions {"/DEF:\"../../../src/engine/engine.def\""}
		end


	defaultConfigurations()

if build_physics then
	project "physics"
		libType()

		files { "../src/physics/**.h", "../src/physics/**.cpp" }

		includedirs { "../external/physx/include/", "../external/bgfx/include" }
		defines { "BUILDING_PHYSICS" }
		links { "engine", "renderer", "editor" }

		useLua()
		linkPhysX()

		defaultConfigurations()
end


project "renderer"
	libType()

	files { "../src/renderer/**.h", "../src/renderer/**.cpp" }
	includedirs { "../src", "../external/bgfx/include", "../external/assimp/include", "../external/crnlib/include" }
	defines { "BUILDING_RENDERER" }
	links { "engine" }

	if build_studio then
		links { "editor", "shaderc" }
		linkLib "crnlib"
		linkLib "assimp"
	end
	linkLib "bgfx"
	configuration { "linux-*" }
		links { "GL", "X11" }
	configuration {}
	useLua()
	
	configuration { "windows", "not asmjs" }
		links { "psapi" }

	defaultConfigurations()

project "animation"
	libType()

	files { "../src/animation/**.h", "../src/animation/**.cpp" }
	includedirs { "../src" }
	includedirs { "../external/bgfx/include" }
	defines { "BUILDING_ANIMATION" }
	links { "engine", "renderer" }

	if build_studio then
		links { "editor" }
	end
	
	useLua()
	defaultConfigurations()

if build_game then
	dofile ("../../" .. build_game .. "/genie.lua")
end
	
if build_steam then
	project "steam"
		libType()
		files { 
			"../src/steam/**.cpp",
			"../src/steam/**.h"
		}
		includedirs { "../src", "../src/steam", "../../steamworks_sdk/public/steam", "../external/bgfx/include" }
		libdirs { "../../steamworks_sdk/redistributable_bin/win64" }
		defines { "BUILDING_STEAM" }
		links { "engine", "steam_api64" }
		useLua()
		defaultConfigurations()
end
	
project "audio"
	libType()

	files { 
		"../src/audio/**.h",
		"../src/audio/**.cpp",
		"../external/stb/stb_vorbis.cpp"
	}
	includedirs { "../src", "../src/audio", "../external/bgfx/include" }
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
	
project "navigation"
	libType()

	files { "../src/navigation/**.h", "../src/navigation/**.cpp", "../external/recast/src/**.cpp" }
	includedirs { "../src", "../src/navigation", "../external/recast/include" }
	includedirs { "../external/bgfx/include" }
	links { "engine", "renderer" }
	linkLib "recast"
	
	if build_studio then
		links { "editor" }
	end
	
	useLua()
	defaultConfigurations()

if build_gui then
	project "gui"
		libType()

		files { "../src/gui/**.h", "../src/gui/**.cpp" }
		includedirs { "../src", "../src/gui", "../external/bgfx/include" }
		links { "engine", "renderer" }
		linkLib "bgfx"
		
		configuration { "vs*" }
			links { "winmm", "psapi" }
		configuration {}

		if build_studio then
			links { "editor" }
		end
	
		useLua()
		defaultConfigurations()
end
	
project "lua_script"
	libType()

	files { "../src/lua_script/**.h", "../src/lua_script/**.cpp" }
	includedirs { "../src", "../src/lua_script", "../external/bgfx/include" }
	defines { "BUILDING_LUA_SCRIPT" }
	links { "engine", "renderer" }

	if build_studio then
		links { "editor" }
	end

	useLua()
	defaultConfigurations()

if build_unit_tests then
	project "unit_tests"
		kind "ConsoleApp"
		debugdir "../../LumixEngine_data"

		files { "../src/unit_tests/**.h", "../src/unit_tests/**.cpp" }
		includedirs { "../src", "../src/unit_tests", "../external/bgfx/include" }
		links { "animation", "renderer", "engine" }
		if _OPTIONS["static-plugins"] then	
			configuration { "vs*" }
				links { "winmm", "psapi" }
			configuration {} 
				linkLib "bgfx"
		end

		useLua()
		defaultConfigurations()
end


if build_app then
	project "app"
		debugdir "../../LumixEngine_data"
		kind "ConsoleApp"
		
		includedirs { "../src", "../src/app", "../external/bgfx/include" }
		if _OPTIONS["static-plugins"] then	
			forceLink("s_animation_plugin_register")
			forceLink("s_audio_plugin_register")
			forceLink("s_lua_script_plugin_register")
			forceLink("s_navigation_plugin_register")
			forceLink("s_renderer_plugin_register")
			
			if build_gui then
				forceLink("s_gui_plugin_register")
			end

			if build_studio then
				forceLink("setStudioApp_animation")
				forceLink("setStudioApp_audio")
				forceLink("setStudioApp_lua_script")
				forceLink("setStudioApp_navigation")
				forceLink("setStudioApp_renderer")
				--forceLink("setStudioApp_gui")
			end
				
			if build_physics then
				forceLink("s_physics_plugin_register")
				if build_studio then
					forceLink("setStudioApp_physics")
				end
				links { "physics" }
				linkPhysX()
			end
			if build_gui then
				links { "gui" }
			end
			links { "audio", "animation", "renderer", "lua_script", "navigation" }
			if build_studio then links {"editor"} end
			links {"engine"}
			
			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }

			configuration {}
		else
			links { "renderer", "editor", "engine" }
		end
		if build_studio then
			linkLib "crnlib"
			linkLib "assimp"
			links {"shaderc"}
		end
		
		linkLib "bgfx"
		linkLib "lua"
		linkLib "recast"
		
		configuration { "asmjs" }
			targetextension ".bc"
			files { "../src/app/main_asmjs.cpp" }

		configuration { "windows", "not android-*" }
			kind "WindowedApp"

		configuration { "windows", "not asmjs", "not android-*" }
			files { "../src/app/main_win.cpp" }

		configuration { "linux-*" }
			files { "../src/app/main_linux.cpp" }
			links { "GL", "X11", "dl", "rt" }
		
		configuration {}
		
		linkLib "SDL"
		configuration {"vs*"}
			links { "winmm", "imm32", "version" }
		configuration {}

		useLua()
		defaultConfigurations()
		strip()
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

		configuration { "windows", "not asmjs" }
			links { "winmm" }

		configuration {}

		if not _OPTIONS["static-plugins"] then	
			linkLib "SDL"
			configuration {"vs*"}
				links { "winmm", "imm32", "version" }
			configuration {}
		end
		
		useLua()
		defaultConfigurations()

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
			forceLink("s_renderer_plugin_register")
			if build_steam then
				forceLink("s_steam_plugin_register")
				links { "steam", "steam_api64" }
				libdirs { "../../steamworks_sdk/redistributable_bin/win64" }
			end

			if build_game then
				forceLink("s_game_plugin_register")
				links { "game" }
			end
			
			forceLink("setStudioApp_animation")
			forceLink("setStudioApp_audio")
			forceLink("setStudioApp_lua_script")
			forceLink("setStudioApp_navigation")
			forceLink("setStudioApp_renderer")

			if build_physics then
				forceLink("s_physics_plugin_register")
				forceLink("setStudioApp_physics")
				links { "physics" }
			end

			if build_gui then
				forceLink("s_gui_plugin_register")
				links { "gui" }
			end

			links { "audio", "animation", "renderer", "lua_script", "navigation", "editor", "engine", "shaderc" }
			linkLib "crnlib"
			linkLib "assimp"
			linkLib "bgfx"
			linkLib "lua"
			linkLib "recast"
			
			linkPhysX()
			
			configuration { "linux-*" }
				links { "GL", "X11", "dl", "rt" }
				linkoptions { "-Wl,-rpath '-Wl,$$ORIGIN'" }

			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }
			
			configuration {}
		else
			links { "renderer", "editor", "engine" }
		end

		linkLib "SDL"

		configuration {"vs*"}
			links { "winmm", "imm32", "version" }
		configuration {}
		
		useLua()
		defaultConfigurations()
		
		if _ACTION == "vs2015" then
			copyDlls("Debug", "win64", "Debug")
			copyDlls("Release", "win64", "Release")
			copyDlls("Release", "win64", "RelWithDebInfo")
		end
		
		if "linux-gcc" == _OPTIONS["gcc"] then
			copyDlls("debug", "linux64", "Debug")
			copyDlls("release", "linux64", "Release")
		end
end
