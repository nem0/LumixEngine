local ide_dir = iif(_ACTION == nil, "vs2015", _ACTION)
if _ACTION == "gmake" then
	if "linux-gcc" == _OPTIONS["gcc"] then
		ide_dir = "gcc"
	elseif "linux-gcc-5" == _OPTIONS["gcc"] then
		ide_dir = "gcc5"
	elseif "linux-clang" == _OPTIONS["gcc"] then
		ide_dir = "clang"
	end
end

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local build_physics = true
local build_unit_tests = true
local build_app = true
local build_studio = true

newoption {
	trigger = "static-plugins",
	description = "Plugins are static libraries."
}

newoption {
	trigger = "no-physics",
	description = "Do not build physics plugin."
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
	trigger = "no-studio",
	description = "Do not build Studio."
}

if _OPTIONS["no-physics"] then
	build_physics = false
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

newaction {
	trigger = "install32",
	description = "Install 32bit in ../../LumixEngine_data/bin32",
	execute = function()
		local src_dir = "tmp/vs2015/bin/RelWithDebInfo/"
		local dst_dir = "../../LumixEngine_data/bin32/"
		
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
		
		installDll "PhysX3CommonCHECKED_x86"
		installDll "PhysX3CookingCHECKED_x86"
		installDll "PhysX3CharacterKinematicCHECKED_x86"
		installDll "PhysX3CHECKED_x86"
		installDll "nvToolsExt32_1"

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

	for _,platform_bit in ipairs({"32", "64"}) do
		for conf,conf_dir in pairs({Debug="debug", Release="release", RelWithDebInfo="release"}) do
			for platform,target_platform in pairs({win="windows", linux="linux", }) do
				configuration { "x" .. platform_bit, conf, target_platform }
					libdirs {"../external/" .. lib .. "/lib/" .. platform .. platform_bit .. "_" .. ide_dir .. "/" .. conf_dir}
			end
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

function copyDlls(src_dir, platform_bit, platform_dir, dest_dir)
	local physx_suffix
	if platform_bit == 32 then
		configuration { "x32", dest_dir, "windows" }
		physx_suffix = "x86"
	else
		configuration { "x64", dest_dir, "windows" }
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
	if build_physics then
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
end

function forceLink(name)

	configuration { "linux-*" }
		linkoptions {"-u " .. name}
	configuration { "x64", "vs*" }
		linkoptions {"/INCLUDE:" .. name}
	configuration { "x32", "vs*" }
		linkoptions {"/INCLUDE:_" .. name}
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
		excludes { "../src/**/editor/*" }
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
		links {
			"rt",
			"dl",
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
	platforms { "x32", "x64" }
	flags { 
		"FatalWarnings", 
		"NoPCH", 
		"NoExceptions", 
		"NoRTTI", 
		"NoEditAndContinue"
	}
	includedirs {"../src", "../external"}
	location(LOCATION)
	language "C++"
	startproject "studio"

	configuration { "vs*" }
		defines { "_HAS_EXCEPTIONS=0" }

	configuration "not macosx"
		excludes { "../src/**/osx/*"}
		
	configuration "not linux"
		excludes { "../src/**/linux/*"}
		
	configuration "not windows"
		excludes { "../src/**/win/*"}

	configuration "asmjs"
		excludes { "../src/**/win/*"}

	configuration "android-*"
		excludes { "../src/**/win/*"}
		
	configuration "not asmjs" 
		excludes { "../src/**/asmjs/*"}
	
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
	linkLib("lua")

	configuration { "windows", "not asmjs", "not android-*" }
		if not _OPTIONS["static-plugins"] then
			linkoptions {"/DEF:\"../../../src/engine/engine.def\""}
		end
		
	defaultConfigurations()

if build_physics then
	project "physics"
		libType()

		files { "../src/physics/**.h", "../src/physics/**.cpp" }

		includedirs { "../external/physx/include/" .. ide_dir, "../external/bgfx/include" }
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
		links { "editor" }
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
	
	defaultConfigurations()

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

	files { "../src/navigation/**.h", "../src/navigation/**.cpp" }
	includedirs { "../src", "../src/navigation", "../external/recast/include" }
	includedirs { "../external/bgfx/include" }
	links { "engine", "renderer" }
	linkLib "recast"
	
	if build_studio then
		links { "editor" }
	end
	
	useLua()
	defaultConfigurations()
	
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
			forceLink("s_physics_plugin_register")
			forceLink("s_renderer_plugin_register")

			if build_studio then
				forceLink("setStudioApp_animation")
				forceLink("setStudioApp_audio")
				forceLink("setStudioApp_lua_script")
				forceLink("setStudioApp_physics")
				forceLink("setStudioApp_renderer")
			end
				
			links { "audio", "animation", "renderer", "lua_script", "navigation", "engine" }
			
			if build_physics then
				links { "physics" }
			end

			configuration { "vs*" }
				links { "psapi", "dxguid", "winmm" }

			configuration { "asmjs" }
				links { "SDL" }
				
			configuration {}
			
			if build_studio then
				linkLib "crnlib"
				linkLib "assimp"
			end
			
			linkLib "bgfx"
			linkLib "lua"
			linkLib "recast"
			linkPhysX()
		else
			links { "renderer", "engine" }
		end
		
		configuration { "asmjs" }
			targetextension ".bc"
			files { "../src/app/main_asmjs.cpp" }

		configuration { "windows", "not android-*" }
			kind "WindowedApp"

		configuration { "windows", "not asmjs", "not android-*" }
			files { "../src/app/main_win.cpp" }

		configuration { "linux-*" }
			files { "../src/app/main_linux.cpp" }
			links { "GL", "X11" }
		
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
			forceLink("s_physics_plugin_register")
			forceLink("s_renderer_plugin_register")

			forceLink("setStudioApp_animation")
			forceLink("setStudioApp_audio")
			forceLink("setStudioApp_lua_script")
			forceLink("setStudioApp_physics")
			forceLink("setStudioApp_renderer")

			links { "engine", "audio", "animation", "renderer", "lua_script", "navigation" }
			if build_physics then
				links { "physics" }
			end
			linkLib "crnlib"
			linkLib "assimp"
			linkLib "bgfx"
			linkLib "lua"
			linkLib "recast"
			
			linkPhysX()
			
			configuration { "windows", "not asmjs" }
				links { "psapi", "dxguid", "winmm" }
			
			configuration {}
		end
		links { "editor" }

		useLua()
		defaultConfigurations()
		
		copyDlls("Debug", 32, "win32", "Debug")
		copyDlls("Debug", 64, "win64", "Debug")
		copyDlls("Release", 32, "win32", "Release")
		copyDlls("Release", 64, "win64", "Release")
		copyDlls("Release", 32, "win32", "RelWithDebInfo")
		copyDlls("Release", 64, "win64", "RelWithDebInfo")
end
