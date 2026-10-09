-- Helpers for plugins that live outside this repository. Loaded with dofile() from the plugin's premake5.lua.
-- Port of the old Genie helpers. Paths are the same as before; the prebuilt library layout is the one this
-- repo uses (external/<lib>/lib/win and external/<lib>/dll/win).

local ide_dir = _ACTION or "vs2022"
local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local ENGINE_ROOT = path.getabsolute("../")

-- default settings for plugin projects
function defaultConfigurations()
	filter "configurations:Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "NDEBUG", "LUMIX_DEBUG" }
		runtime "Release"
		symbols "Full"

	filter "configurations:RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		symbols "Full"
		optimize "On"

	filter "system:linux"
		defines { "_GLIBCXX_USE_CXX11_ABI=0" }
		links { "pthread" }

	filter "action:vs*"
		buildoptions { "/wd4503" }

	filter {}
		files {
			path.join(ENGINE_ROOT, "./src/lumix.natvis"),
			path.join(ENGINE_ROOT, ".editorconfig")
		}
		defines { "_ITERATOR_DEBUG_LEVEL=0", "STBI_NO_STDIO" }
end

-- prebuilt library from external/<lib>
function linkLib(lib)
	links { lib }
	filter { "platforms:x64", "system:windows" }
		libdirs { path.join(ENGINE_ROOT, "./external/" .. lib .. "/lib/win"), path.join(ENGINE_ROOT, "./external/" .. lib .. "/dll/win") }
	filter {}
end

function useLua()
	linkLib("lua51")
	linkLib("luajit")
	includedirs { path.join(ENGINE_ROOT, "./external/luajit/include") }
end

-- workspace for a plugin, same flags as the Lumix workspace
function makeSolution(name)
	workspace(name)
		configurations { "Debug", "Release", "RelWithDebInfo" }
		platforms { "x64" }
		location(LOCATION)
		language "C++"
		cppdialect "C++17"
		startproject "studio"
		exceptionhandling "Off"
		rtti "Off"
		editandcontinue "Off"
		fatalwarnings { "All" }
		includedirs { "../src", "../external" }

		filter "action:vs*"
			defines { "_HAS_EXCEPTIONS=0" }

		filter "system:not linux"
			removefiles { "../src/**/linux/*" }

		filter "system:not windows"
			removefiles { "../src/**/win/*" }

		filter {}
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

		filter "configurations:Debug"
			libdirs { "../lumixengine/scripts/tmp/" .. ide_dir .. "/bin/Debug" }

		filter "configurations:Release"
			libdirs { "../lumixengine/scripts/tmp/" .. ide_dir .. "/bin/Release" }

		filter "configurations:RelWithDebInfo"
			libdirs { "../lumixengine/scripts/tmp/" .. ide_dir .. "/bin/RelWithDebInfo" }

		filter {}
end
