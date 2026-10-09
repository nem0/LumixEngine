-- LumixEngine build script. Run from this directory: premake5 vs2022
-- Paths are relative to scripts/, like the old build scripts.

local LOCATION = "tmp/" .. _ACTION .. (_OPTIONS["dynamic-plugins"] and "-dynamic" or "")
local BINARY_DIR = LOCATION .. "/bin/"

newoption { trigger = "dynamic-plugins", description = "Build plugins as shared libraries" }
newoption { trigger = "no-studio", description = "Do not build Studio" }
newoption { trigger = "with-vulkan", description = "Use the minimal Vulkan clear/present backend (always on Linux)" }
newoption { trigger = "no-app", description = "Do not build the app (game runtime without Studio)" }
-- PhysX is always linked statically (PX_PHYSX_STATIC_LIB), the option is kept for the scripts that pass it
newoption { trigger = "static-physx", description = "Link PhysX statically (the default)" }
newoption { trigger = "with-tests", description = "Build test projects (requires the evox plugin)" }
newoption { trigger = "plugins", value = "LIST", description = "Comma-separated list of plugins to build" }

local ALL_PLUGINS = { "physics", "renderer", "audio", "ui", "animation", "navigation", "evox" }
for _, name in ipairs(ALL_PLUGINS) do
	newoption { trigger = "no-" .. name, description = "Do not build " .. name .. " plugin" }
end

local dynamic_plugins = _OPTIONS["dynamic-plugins"] ~= nil
local build_studio = _OPTIONS["no-studio"] == nil
local build_vulkan = _OPTIONS["with-vulkan"] ~= nil or os.target() == "linux"
local build_app = _OPTIONS["no-app"] == nil
local build_tests = _OPTIONS["with-tests"] ~= nil

-- Static builds merge core, engine and plugins into one library.
-- Dynamic builds keep core and engine separate and build each plugin as its own DLL.
local CORE_NAME = dynamic_plugins and "core" or "engine_merged"
local ENGINE_NAME = dynamic_plugins and "engine" or "engine_merged"
local EDITOR_NAME = dynamic_plugins and "editor" or "engine_merged"
-- in dynamic builds core, engine and editor are DLLs that plugins link to, the plugins export
local LIB_KIND = dynamic_plugins and "SharedLib" or "StaticLib"

local plugins = {}
if _OPTIONS["plugins"] then
	plugins = string.explode(_OPTIONS["plugins"], ",")
end
for _, name in ipairs(ALL_PLUGINS) do
	if _OPTIONS["no-" .. name] == nil then
		table.insert(plugins, name)
	end
end
local function hasPlugin(name)
	for _, v in ipairs(plugins) do
		if v == name then return true end
	end
	return false
end

if build_tests and not hasPlugin("evox") then
	error("--with-tests requires the evox plugin")
end
-- editor.dll would need renderer.dll (imgui tests use Texture), and renderer.dll already needs editor.dll: a circular dependency
if build_tests and dynamic_plugins then
	error("--with-tests cannot be combined with --dynamic-plugins yet (renderer and editor depend on each other)")
end

-- plugins/* are local plugins, each one has a premake5.lua that is run in its own project
for _, dir in ipairs(os.matchdirs("../plugins/*")) do
	table.insert(plugins, path.getname(dir))
end

-- Sources of other platforms are excluded by pattern. Chosen from the target OS, not a filter,
-- since filtered excludes did not apply to every project.
local platform_excludes = {}
if os.target() ~= "linux" then table.insert(platform_excludes, path.getabsolute("../src") .. "/**/linux/*") end
if os.target() ~= "windows" then table.insert(platform_excludes, path.getabsolute("../src") .. "/**/win/*") end
if os.target() ~= "macosx" then table.insert(platform_excludes, path.getabsolute("../src") .. "/**/osx/*") end

-- premake5 only applies excludes to files that were already added, so this must be called
-- at the end of every project, after its files.
local function excludeSources()
	excludes(platform_excludes)
	if not build_studio then
		excludes { "../src/**/editor/*" }
	end
end

-- Default settings shared by every project.
local function defaultConfigurations()
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

	filter {}
		defines { "_ITERATOR_DEBUG_LEVEL=0", "STBI_NO_STDIO" }
		files { "../src/lumix.natvis", ".editorconfig" }
end

-- Creates the project, or selects it if it was already created (static builds share engine_merged).
local created_projects = {}
local function beginProject(name, projectKind)
	project(name)
	if not created_projects[name] then
		created_projects[name] = true
		kind(projectKind)
		defaultConfigurations()
	end
end

-- Prebuilt third-party libraries from external/<lib>/{lib,dll}/{win,linux}
local function linkLib(lib)
	links { lib }
	filter { "platforms:x64", "system:windows" }
		libdirs { "../external/" .. lib .. "/lib/win", "../external/" .. lib .. "/dll/win" }
	filter { "platforms:x64", "system:linux" }
		libdirs { "../external/" .. lib .. "/lib/linux", "../external/" .. lib .. "/dll/linux" }
	filter {}
end

-- Prebuilt PhysX from external/physx/lib/win
local function linkPhysX()
	defines { "PX_PHYSX_STATIC_LIB", "PX_PHYSX_CHARACTER_STATIC_LIB" }
	linkLib "PhysX"
end

-- Plugins link other plugins only in dynamic builds. Static libs must not link each other, it only serializes the build.
local function linkPluginDeps(deps)
	if dynamic_plugins then
		links(deps)
		-- plugins use editor code when Studio is built, as before
		if build_studio then
			links { "editor" }
		end
	end
end

workspace "LumixEngine"
	configurations { "Debug", "RelWithDebInfo" }
	platforms { "x64" }
	location(LOCATION)
	language "C++"
	cppdialect "C++20"
	characterset "MBCS" -- no CharacterSet element, as before
	startproject "studio"

	exceptionhandling "Off"
	rtti "Off"
	editandcontinue "Off"
	fatalwarnings { "All" }
	linkgroups "On"

	includedirs { "../src", "../external" }

	if dynamic_plugins then
		defines { "EVOX_SHARED" }
	else
		defines { "STATIC_PLUGINS" }
	end

	if build_tests then
		defines { "LUMIX_TESTS" }
	end

	filter "system:linux"
		buildoptions {
			"-m64", "-fPIC", "-no-canonical-prefixes", "-Wa,--noexecstack", "-fstack-protector",
			"-ffunction-sections", "-Wunused-value", "-Wundef", "-msse2", "-Wno-multichar",
			"-Wno-undef", "-Wno-ignored-attributes", "-Wno-psabi"
		}
		linkoptions { "-Wl,--gc-sections", "-fopenmp" }

	filter "action:vs*"
		defines { "_HAS_EXCEPTIONS=0", "_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS" }
		-- /FS: parallel cl.exe processes share one pdb per project
		buildoptions { "/Zc:char8_t-", "/FS", "/MP" }

	filter { "action:vs*", "configurations:RelWithDebInfo" }
		buffersecuritycheck "Off"
		buildoptions { "/GL", "/Oi" }
		linkoptions { "/LTCG:incremental" }

	filter { "action:vs*", "language:C++" }
		buildoptions { "/wd4503", "/wd4251" }

	filter {}

-- Tools

project "meta"
	kind "ConsoleApp"
	debugdir "../"
	defaultConfigurations()
	files { "../src/meta/**.cpp", "../src/meta/**.h" }
	excludeSources()

-- Core

beginProject(CORE_NAME, LIB_KIND)
	defines { "BUILDING_CORE" }
	files {
		"../src/core/**.h", "../src/core/**.c", "../src/core/**.cpp", "../src/core/**.inl",
		"../external/wyhash/**.*"
	}
	excludeSources()
	filter "system:linux"
		buildoptions { "`pkg-config --cflags gtk+-3.0`" }
	filter {}

	-- Regenerate reflection code before building core. Windows only, the meta tool is not portable yet.
	filter "system:windows"
		dependson { "meta" }
		prebuildcommands {
			"msbuild $(SolutionDir)meta.vcxproj /p:Configuration=$(Configuration) /p:Platform=$(Platform) /verbosity:minimal",
			"cd $(ProjectDir)../../../ && $(SolutionDir)bin\\$(Configuration)\\meta.exe"
		}
	filter {}

-- Engine

beginProject(ENGINE_NAME, LIB_KIND)
	defines { "BUILDING_ENGINE", "EVOX_BUILD" }
	if dynamic_plugins then
		dependson { CORE_NAME }
		links { CORE_NAME }
	end
	includedirs { "../src", "../external/freetype/include" }
	files {
		"../src/engine/**.h", "../src/engine/**.c", "../src/engine/**.cpp", "../src/engine/**.inl",
		"../external/imgui/**.h", "../external/imgui/**.cpp", "../external/imgui/**.inl",
		"../external/lz4/**.c", "../external/lz4/**.h",
		-- EVOX script compiler and runtime
		"../external/evox/**.cpp", "../external/evox/**.c", "../external/evox/**.h"
	}
	-- imgui is built through imgui_unity.cpp, which includes these files
	excludes {
		"../external/imgui/imgui_demo.cpp", "../external/imgui/imgui.cpp", "../external/imgui/imgui_tables.cpp",
		"../external/imgui/imgui_draw.cpp", "../external/imgui/imgui_widgets.cpp", "../external/imgui/imgui_freetype.cpp",
		-- evoxc is the command line compiler, tests and benchmarks are not part of the library
		"../external/evox/evoxc.c", "../external/evox/tests/*.cpp", "../external/evox/benchmarks/**.*"
	}
	filter "system:linux"
		buildoptions { "`pkg-config --cflags gtk+-3.0`" }
	filter {}
	linkLib "freetype"
	if dynamic_plugins then
		defines { "LZ4_DLL_EXPORT" }
	end
	-- dynamic builds: engine.dll has to provide the imgui test engine hooks itself
	if build_tests and dynamic_plugins then
		includedirs { "../external/imgui_test_engine", "../external/imgui" }
		files { "../external/imgui_test_engine/**.h", "../external/imgui_test_engine/**.cpp" }
	end
	excludeSources()

-- PhysX from source, when external/_repos/physx exists (scripts/download_physx.bat). Otherwise the prebuilt
-- external/physx/lib/win/PhysX.lib is used. The built library goes to the same place as the prebuilt one.
local build_physx = os.isdir("../external/_repos/physx")
if build_physx and hasPlugin("physics") then
	printf("Using PhysX from external/_repos/physx (build from source code)")
	project "PhysX"
		kind "StaticLib"
		files {
			"../external/_repos/physx/physx/source/**.cpp",
			"../external/_repos/physx/physx/include/**.h",
			"../external/_repos/physx/pxshared/**.h"
		}
		removefiles { "../external/_repos/physx/**/unix/*", "../external/_repos/physx/**/linux/*" }
		includedirs {
			"../external/_repos/physx/physx/include",
			"../external/_repos/physx/pxshared/include",
			"../external/_repos/physx/physx/source/common/include",
			"../external/_repos/physx/physx/source/fastxml/include",
			"../external/_repos/physx/physx/source/filebuf/include",
			"../external/_repos/physx/physx/source/foundation/include",
			"../external/_repos/physx/physx/source/geomutils/include",
			"../external/_repos/physx/physx/source/lowlevel/api/include",
			"../external/_repos/physx/physx/source/lowlevel/common/include",
			"../external/_repos/physx/physx/source/lowlevel/common/include/collision",
			"../external/_repos/physx/physx/source/lowlevel/common/include/pipeline",
			"../external/_repos/physx/physx/source/lowlevel/common/include/utils",
			"../external/_repos/physx/physx/source/lowlevel/software/include",
			"../external/_repos/physx/physx/source/lowlevelaabb/include",
			"../external/_repos/physx/physx/source/lowleveldynamics/include",
			"../external/_repos/physx/physx/source/physx/src",
			"../external/_repos/physx/physx/source/physx/src/buffering",
			"../external/_repos/physx/physx/source/physx/src/device",
			"../external/_repos/physx/physx/source/physx/src/gpu",
			"../external/_repos/physx/physx/source/physx/src/windows",
			"../external/_repos/physx/physx/source/physxgpu/include",
			"../external/_repos/physx/physx/source/physxmetadata/core/include",
			"../external/_repos/physx/physx/source/physxmetadata/extensions/include",
			"../external/_repos/physx/physx/source/physxvehicle/src/physxmetadata/include",
			"../external/_repos/physx/physx/source/pvd/include",
			"../external/_repos/physx/physx/source/scenequery/include",
			"../external/_repos/physx/physx/source/simulationcontroller/include",
			"../external/_repos/physx/physx/source/physxcooking/src/",
			"../external/_repos/physx/physx/source/physxcooking/src/convex",
			"../external/_repos/physx/physx/source/physxcooking/src/mesh",
			"../external/_repos/physx/physx/source/physxextensions/src",
			"../external/_repos/physx/physx/source/physxextensions/src/serialization",
			"../external/_repos/physx/physx/source/physxextensions/src/serialization/Binary",
			"../external/_repos/physx/physx/source/physxextensions/src/serialization/File",
			"../external/_repos/physx/physx/source/physxextensions/src/serialization/Xml",
			"../external/_repos/physx/physx/source/physxvehicle/src",
			"../external/_repos/physx/physx/source/simulationcontroller/src",
			"../external/_repos/physx/physx/source/foundation/src",
			"../external/_repos/physx/physx/source/common/src",
			"../external/_repos/physx/physx/source/fastxml/src",
			"../external/_repos/physx/physx/source/geomutils/src",
			"../external/_repos/physx/physx/source/geomutils/src/**"
		}
		defines {
			"NDEBUG", "PX_PHYSX_STATIC_LIB", "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS", "PX_COOKING"
		}
		-- no debug info, as before. Shared PDBs make the parallel cl.exe runs fail
		symbols "Off"
		optimize "Size"
		runtime "Release"
		filter "system:windows"
			targetdir "../external/physx/lib/win"
		filter "action:vs20*"
			buildoptions { "/wd5055" }
		filter {}
end

-- FreeType from source, when external/_repos/freetype exists (scripts/download_freetype.bat). Otherwise the
-- prebuilt external/freetype/lib/win/freetype.lib is used. The built library goes to the same place as the prebuilt one.
if os.isdir("../external/_repos/freetype") then
	printf("Using FreeType from external/_repos/freetype (build from source code)")
	project "freetype"
		kind "StaticLib"
		targetname "freetype"
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
		-- third party code, lower warning level and no warnings as errors
		warnings "Default"
		-- the workspace sets fatal warnings, and premake cannot clear it, so MSVC's /WX is turned off here
		filter "action:vs*"
			buildoptions { "/WX-" }
		filter {}
		defines { "NDEBUG", "FT2_BUILD_LIBRARY", "_CRT_SECURE_NO_WARNINGS" }
		-- no debug info, as before. Shared PDBs make the parallel cl.exe runs fail
		symbols "Off"
		runtime "Release"
		filter "system:windows"
			targetdir "../external/freetype/lib/win"
		filter "system:linux"
			targetdir "../external/freetype/lib/linux"
		filter "action:vs20*"
			buildoptions { "/wd4312" }
		filter {}
else
	printf("Using FreeType from external/freetype (prebuilt)")
end

-- Plugins. Each one adds its sources and settings. In static builds they go into engine_merged.

local plugin_defs = {}
local plugin_creators = {}

plugin_defs.physics = function()
	files { "../src/physics/**.h", "../src/physics/**.cpp" }
	includedirs { "../external/physx/include/" }
	defines { "BUILDING_PHYSICS", "LUMIX_STATIC_PHYSX" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME, "renderer" }
	linkPhysX()
end

plugin_defs.renderer = function()
	files { "../src/renderer/**.h", "../src/renderer/**.cpp", "../src/renderer/**.c", "../data/shaders/**.*" }
	excludes {
		"../external/meshoptimizer/clusterizer.cpp", "../external/meshoptimizer/overdrawanalyzer.cpp",
		"../external/meshoptimizer/overdrawoptimizer.cpp", "../external/meshoptimizer/spatialorder.cpp",
		"../external/meshoptimizer/stripifier.cpp", "../external/meshoptimizer/vcacheanalyzer.cpp",
		"../external/meshoptimizer/vcacheoptimizer.cpp", "../external/meshoptimizer/vertexcodec.cpp",
		"../external/meshoptimizer/vertexfilter.cpp", "../external/meshoptimizer/vfetchanalyzer.cpp",
		"../external/meshoptimizer/vfetchoptimizer.cpp",
		-- includes "engine/math.h", which does not exist
		"../src/renderer/editor/voxelizer_ui.cpp"
	}
	-- shaders are only data, compiled at runtime. Visual Studio would try to compile .hlsl files otherwise.
	filter "files:../data/shaders/**"
		buildaction "None"
	filter {}

	if build_studio then
		files { "../external/meshoptimizer/**.*", "../external/mikktspace/**.*", "../external/openfbx/**.*" }
	end
	includedirs { "../src", "../external/freetype/include", "../external/", "../external/dx12/", "../external/pix/include/WinPixEventRuntime" }
	defines { "BUILDING_RENDERER" }
	libdirs { "../external/pix/bin/x64" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME }
	linkLib "freetype"

	filter "system:linux"
		links { "GL", "X11", "Xi", "vulkan" }
		excludes { "../src/renderer/gpu/gpu_dx12.cpp" }

	filter "system:windows"
		links { "psapi", "dxguid" }
	filter {}

	if build_vulkan then
		local vulkan_sdk = os.getenv("VULKAN_SDK")
		if vulkan_sdk then
			includedirs { vulkan_sdk .. "/Include" }
			libdirs { vulkan_sdk .. "/Lib" }
		else
			printf("VULKAN_SDK is not set; install the Vulkan SDK before building with --with-vulkan")
		end
		filter "system:windows"
			links { "vulkan-1" }
			excludes { "../src/renderer/gpu/gpu_dx12.cpp" }
		filter {}
	else
		filter "system:windows"
			excludes { "../src/renderer/gpu/gpu_vulkan.cpp" }
		filter {}
	end
end

plugin_defs.animation = function()
	files { "../src/animation/**.h", "../src/animation/**.cpp" }
	includedirs { "../src" }
	defines { "BUILDING_ANIMATION" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME, "renderer" }
end

plugin_defs.audio = function()
	files { "../src/audio/**.h", "../src/audio/**.cpp", "../external/stb/stb_vorbis.cpp" }
	includedirs { "../src", "../src/audio" }
	defines { "BUILDING_AUDIO" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME }
	filter "system:windows"
		links { "dxguid" }
	filter {}
end

plugin_defs.navigation = function()
	files {
		"../src/navigation/**.h", "../src/navigation/**.cpp",
		"../external/recast/src/**.cpp", "../external/recast/include/**.h"
	}
	-- recast is built as a unity build (recast_unity.cpp), except these two which have conflicting symbols
	excludes {
		"../external/recast/src/DetourAlloc.cpp", "../external/recast/src/DetourAssert.cpp",
		"../external/recast/src/DetourCommon.cpp", "../external/recast/src/DetourCrowd.cpp",
		"../external/recast/src/DetourLocalBoundary.cpp", "../external/recast/src/DetourNavMesh.cpp",
		"../external/recast/src/DetourNavMeshBuilder.cpp", "../external/recast/src/DetourNavMeshQuery.cpp",
		"../external/recast/src/DetourNode.cpp", "../external/recast/src/DetourObstacleAvoidance.cpp",
		"../external/recast/src/DetourPathCorridor.cpp", "../external/recast/src/DetourPathQueue.cpp",
		"../external/recast/src/DetourProximityGrid.cpp", "../external/recast/src/Recast.cpp",
		"../external/recast/src/RecastAlloc.cpp", "../external/recast/src/RecastArea.cpp",
		"../external/recast/src/RecastAssert.cpp", "../external/recast/src/RecastFilter.cpp",
		"../external/recast/src/RecastLayers.cpp", "../external/recast/src/RecastMesh.cpp",
		"../external/recast/src/RecastRasterization.cpp", "../external/recast/src/RecastRegion.cpp"
	}
	includedirs { "../src", "../src/navigation", "../external/recast/include" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME, "renderer" }
end

plugin_defs.ui = function()
	files { "../src/ui/**.h", "../src/ui/**.cpp" }
	includedirs { "../src", "../src/ui" }
	defines { "BUILDING_UI" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME, "renderer" }
	filter "action:vs*"
		links { "winmm", "psapi" }
	filter {}
end

plugin_defs.evox = function()
	files { "../src/evox/**.h", "../src/evox/**.cpp" }
	includedirs { "../src", "../src/evox" }
	defines { "BUILDING_EVOX" }
	linkPluginDeps { CORE_NAME, ENGINE_NAME, "renderer" }
	-- the evox bindings expose ui, animation and audio types
	if dynamic_plugins then
		for _, dep in ipairs { "ui", "animation", "audio" } do
			if hasPlugin(dep) then
				links { dep }
			end
		end
	end
end

for _, name in ipairs(plugins) do
	local def = plugin_defs[name]
	local localScript = "../plugins/" .. name .. "/premake5.lua"
	if not def and os.isfile(localScript) then
		def = function() dofile(localScript) end
	end

	if def then
		if dynamic_plugins then
			beginProject(name, "SharedLib")
			links { ENGINE_NAME }
		else
			beginProject(ENGINE_NAME, LIB_KIND)
		end
		def()
		excludeSources()
		table.insert(plugin_creators, name)
	end
end

-- dynamic builds find the plugins by name at runtime, so they are passed as a define
local function pluginDefines()
	if not dynamic_plugins or #plugin_creators == 0 then return end
	local names = {}
	for _, name in ipairs(plugin_creators) do
		table.insert(names, '"' .. name .. '"')
	end
	defines { "LUMIXENGINE_PLUGINS=" .. table.concat(names, ",") }
end

-- Studio

if build_studio then
	beginProject(EDITOR_NAME, LIB_KIND)
		defines { "BUILDING_EDITOR" }
		pluginDefines()
		-- the imgui test UI is part of studio in static builds and of editor.dll in dynamic builds
		if build_tests and dynamic_plugins then
			includedirs { "../external/imgui_test_engine", "../external/imgui" }
			files { "../src/tests/imgui**" }
		end
		files { "../src/editor/**.h", "../src/editor/**.cpp" }
		excludeSources()
		includedirs { "../src", "../src/editor", "../external" }
		if dynamic_plugins then
			links { CORE_NAME, ENGINE_NAME }
		end
		filter "system:windows"
			links { "winmm" }
		filter {}

	beginProject("studio", "WindowedApp")
		files { "../src/studio/**.cpp" }
		-- Studio contains the imgui test UI when tests are built
		if build_tests then
			includedirs { "../external/imgui_test_engine", "../external/imgui" }
			if not dynamic_plugins then
				files {
					"../external/imgui_test_engine/**.h",
					"../external/imgui_test_engine/**.cpp",
					"../src/tests/imgui**",
				}
			end
		end
		excludeSources()
		includedirs { "../src" }
		links { CORE_NAME, ENGINE_NAME }
		if dynamic_plugins then
			links { EDITOR_NAME }
		end
		debugdir "../data"
		linkLib "freetype"
		-- static builds have the plugins inside engine_merged, so studio needs their libraries too
		if not dynamic_plugins and hasPlugin("physics") then
			linkPhysX()
		end
		filter "system:windows"
			links { "winmm", "imm32", "version", "shell32", "gdi32", "comdlg32", "advapi32", "ole32" }
			libdirs { "../external/pix/bin/x64" }
			-- main.cpp defines main(), not WinMain
			entrypoint "mainCRTStartup"
			-- absolute source paths, postbuild commands are relative to the project location (tmp/vs2022)
			postbuildcommands {
				'{COPYFILE} "' .. path.getabsolute("../external/dbghelp/dbghelp.dll") .. '" "%{cfg.targetdir}"',
				'{COPYFILE} "' .. path.getabsolute("../external/pix/bin/x64/WinPixEventRuntime.dll") .. '" "%{cfg.targetdir}"'
			}
		filter "system:linux"
			links { "dl", "GL", "X11", "rt", "Xi", "gtk-3", "gobject-2.0" }
		filter {}
end

-- App: the game runtime without Studio

if build_app then
	beginProject("app", "ConsoleApp")
		files { "../src/app/main.cpp" }
		-- LUMIX_TESTS makes imgui call into imgui_test_engine, so the app needs it too (dynamic builds get it from engine.dll)
		if build_tests then
			includedirs { "../external/imgui_test_engine", "../external/imgui" }
			if not dynamic_plugins then
				files { "../external/imgui_test_engine/**.h", "../external/imgui_test_engine/**.cpp" }
			end
		end
		excludeSources()
		includedirs { "../src", "../src/app" }
		debugdir "../data"
		pluginDefines()
		if dynamic_plugins then
			-- plugins are loaded by name at runtime, but the app still links their libraries
			links { CORE_NAME, ENGINE_NAME }
			if build_studio then
				links { EDITOR_NAME }
			end
			for _, name in ipairs(plugin_creators) do
				links { name }
			end
		else
			links { ENGINE_NAME }
			if hasPlugin("physics") then
				linkPhysX()
			end
			linkLib "freetype"
		end
		filter "system:windows"
			kind "WindowedApp"
			entrypoint "mainCRTStartup"
			libdirs { "../external/pix/bin/x64" }
			links { "psapi", "dxguid", "winmm", "imm32", "version" }
		filter "system:linux"
			links { "GL", "X11", "dl", "rt", "Xi" }
		filter {}
		if build_vulkan then
			filter "system:windows"
				local vulkan_sdk = os.getenv("VULKAN_SDK")
				if vulkan_sdk then
					libdirs { vulkan_sdk .. "/Lib" }
				end
				links { "vulkan-1" }
			filter {}
		end
end

-- Tests

if build_tests then
	beginProject("tests", "ConsoleApp")
		files { "../src/tests/**.cpp", "../src/tests/**.h" }
		if not dynamic_plugins then
			files { "../external/imgui_test_engine/**.cpp", "../external/imgui_test_engine/**.h" }
		end
		excludeSources()
		includedirs { "../src", "../src/tests", "../external/imgui_test_engine", "../external/imgui" }
		debugdir "../data"
		if dynamic_plugins then
			links { CORE_NAME, ENGINE_NAME, "evox" }
			if hasPlugin("renderer") then
				links { "renderer" }
			end
		else
			links { ENGINE_NAME }
		end
		linkLib "freetype"
		if hasPlugin("physics") then
			linkPhysX()
		end
		filter "system:windows"
			links { "psapi", "dxguid", "winmm" }
			libdirs { "../external/pix/bin/x64" }
			postbuildcommands {
				'{COPYFILE} "' .. path.getabsolute("../external/pix/bin/x64/WinPixEventRuntime.dll") .. '" "%{cfg.targetdir}"'
			}
		filter "system:linux"
			links { "GL", "X11", "dl", "rt", "Xi", "vulkan" }
		filter {}
end

-- Generated plugin registration, included by engine and studio sources.

local plugins_inl = io.open("../src/engine/plugins.inl", "w")
plugins_inl:write("// generated by premake5.lua\n\n")
plugins_inl:write("#ifdef LUMIX_PLUGIN_DECLS\n")
for _, name in ipairs(plugin_creators) do
	plugins_inl:write('extern "C" ISystem* createPlugin_' .. name .. "(Engine&);\n")
end
plugins_inl:write("#elif defined LUMIX_EDITOR_PLUGINS_DECLS\n")
if not dynamic_plugins then
	for _, name in ipairs(plugin_creators) do
		plugins_inl:write('extern "C" Lumix::StudioApp::IPlugin* setStudioApp_' .. name .. "(StudioApp&);\n")
	end
end
plugins_inl:write("#elif defined LUMIX_EDITOR_PLUGINS\n")
if not dynamic_plugins then
	for _, name in ipairs(plugin_creators) do
		plugins_inl:write("{\n")
		plugins_inl:write("\tStudioApp::IPlugin* plugin = setStudioApp_" .. name .. "(*this);\n")
		plugins_inl:write("\tif (plugin) this->addPlugin(*plugin);\n")
		plugins_inl:write("}\n")
	end
end
plugins_inl:write("#elif defined LUMIX_PLUGINS_STRINGS\n")
if dynamic_plugins then
	for _, name in ipairs(plugin_creators) do
		plugins_inl:write('"' .. name .. '", ')
	end
end
plugins_inl:write("nullptr\n")
plugins_inl:write("#else\n")
if not dynamic_plugins then
	for _, name in ipairs(plugin_creators) do
		plugins_inl:write('if (shouldCreateStaticPlugin(plugins, "' .. name .. '")) {\n')
		plugins_inl:write("\tISystem* p = createPlugin_" .. name .. "(engine);\n")
		plugins_inl:write("\tif (p) engine.getSystemManager().addSystem(p, nullptr);\n")
		plugins_inl:write("}\n")
	end
end
plugins_inl:write("#endif\n")
plugins_inl:close()
