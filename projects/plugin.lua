local binary_api_dir = iif(_ACTION == "vs2022" or _ACTION == "vs2019" or _ACTION == "vs2017", "vs2017", "gmake")

local ide_dir = iif(_ACTION == nil, "vs2022", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local ENGINE_ROOT = path.getabsolute("../")

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

function linkPhysX()
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
		configuration { "linux" }
			libdirs {"../external/physx/lib/linux64_gmake/release"}
			links { 
				"PhysX_static_64",
				"PhysXCharacterKinematic_static_64",
				"PhysXCommon_static_64",
				"PhysXCooking_static_64",
				"PhysXExtensions_static_64",
				"PhysXFoundation_static_64",
				"PhysXPvdSDK_static_64",
				--"PhysXTask_static_64",
				"PhysXVehicle_static_64",
				--"SceneQuery_static_64",
				--"SimulationController_static_64"
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
		configuration { "linux" }
			libdirs {"../external/physx/lib/linux64_gmake/release"}	
			links { 
				"PhysX_static_64",
				"PhysXCharacterKinematic_static_64",
				"PhysXCommon_static_64",
				"PhysXCooking_static_64",
				"PhysXExtensions_static_64",
				"PhysXFoundation_static_64",
				"PhysXPvdSDK_static_64",
				--"PhysXTask_static_64",
				"PhysXVehicle_static_64",
				--"SceneQuery_static_64",
				--"SimulationController_static_64"
			}

		configuration { "Debug" }
			libdirs { path.join(ENGINE_ROOT, "./external/physx/lib/" .. binary_api_dir .. "/win64/release") }

		configuration { "RelWithDebInfo" }
			libdirs { path.join(ENGINE_ROOT, "./external/physx/lib/" .. binary_api_dir .. "/win64/release") }

		configuration {}
			defines {"PX_PHYSX_CHARACTER_STATIC_LIB"}
	end
end

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
		files {
			path.join(ENGINE_ROOT, "./projects/lumix.natvis"),
			path.join(ENGINE_ROOT, ".editorconfig")
		}
		defines { "_ITERATOR_DEBUG_LEVEL=0", "STBI_NO_STDIO" }
		flags { "FullSymbols" } -- VS can't set brekpoints from time to time, only rebuilding several times or using FullSymbols helps
end


function linkLib(lib)
	links {lib}

	for conf,conf_dir in pairs({Debug="release", RelWithDebInfo="release"}) do
		for platform,target_platform in pairs({win="windows", linux="linux", }) do
			configuration { "x64", conf, target_platform }
				libdirs { path.join(ENGINE_ROOT, "./external/" .. lib .. "/lib/" .. platform .. "64" .. "_" .. binary_api_dir .. "/" .. conf_dir) }
				libdirs { path.join(ENGINE_ROOT, "./external/" .. lib .. "/dll/" .. platform .. "64" .. "_" .. binary_api_dir .. "/" .. conf_dir) }
		end
	end
	configuration {}
end

function useLua()
	linkLib("lua51")
	linkLib("luajit")
	includedirs { path.join(ENGINE_ROOT, "./external/luajit/include") }
end


function makeSolution(name)
	solution(name)
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

		configuration { "vs*" }
			defines { "_HAS_EXCEPTIONS=0" }

		configuration "not linux"
			removefiles { "../src/**/linux/*"}
			
		configuration "not windows"
			removefiles { "../src/**/win/*"}
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
			libdirs { "../lumixengine/projects/tmp/" .. ide_dir .. "/bin/Debug" }

		configuration  "Release" 
			libdirs { "../lumixengine/projects/tmp/" .. ide_dir .. "/bin/Release" }

		configuration  "RelWithDebInfo" 
			libdirs { "../lumixengine/projects/tmp/" .. ide_dir .. "/bin/RelWithDebInfo" }

		configuration {}
end

