local ide_dir = iif(_ACTION == nil, "vs2019", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"


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

		
function defaultConfigurations(dir)
	dir = dir or BINARY_DIR
	configuration "Debug"
		targetdir(dir .. "Debug")
		defines { "DEBUG", "_DEBUG" }
		flags { "Symbols", "WinMain" }

	configuration "Release"
		targetdir(dir .. "Release")
		defines { "NDEBUG" }
		flags { "Optimize", "WinMain" }

	configuration "RelWithDebInfo"
		targetdir(dir .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize", "WinMain" }

	configuration "linux"
		buildoptions { "-std=c++14" }
		defines { "_GLIBCXX_USE_CXX11_ABI=0" }
		links { "pthread" }

	configuration { "vs20*"}
		buildoptions { "/wd4503"}
		
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
		configuration { "windows" }
			defines { "LUA_BUILD_AS_DLL" }
		configuration {}
	end
	includedirs { "../external/lua/include" }
end


function makeSolution(name)
	solution(name)
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

		configuration "android-*"
			removefiles { "../src/**/win/*"}
			
		configuration "not asmjs" 
			removefiles { "../src/**/asmjs/*"}
		
		if _OPTIONS["static-plugins"] then
			defines {"STATIC_PLUGINS"}
		end
end

function bootstrapPlugin(name)
	makeSolution(name)

	project(name)
		kind "SharedLib"
		includedirs { 
			"../lumixengine/src/",
			"../lumixengine/external/",
		}
		
		links { "engine" }
		
		configuration  "Debug" 
			libdirs { "../lumixengine/projects/tmp/vs2019/bin/Debug" }

		configuration  "Release" 
			libdirs { "../lumixengine/projects/tmp/vs2019/bin/Release" }

		configuration  "RelWithDebInfo" 
			libdirs { "../lumixengine/projects/tmp/vs2019/bin/RelWithDebInfo" }

		configuration {}
end

