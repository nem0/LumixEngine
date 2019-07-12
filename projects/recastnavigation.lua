solution "RecastDetour"
	location "3rdparty/recast/_project/"
	targetdir "3rdparty/recast/_build/"
	language "C++"
	configurations { "Release" }
	platforms { "x64" }
	flags { 
		"FatalWarnings", 
		"NoPCH", 
		"NoExceptions", 
		"NoRTTI", 
		"NoEditAndContinue"
	}
	
project "Recast"
	kind "StaticLib"
	flags { "Symbols", "ReleaseRuntime", "WinMain" }
	configuration { "Release" }
	files { "3rdparty/recast/Recast/Source/**.cpp" }
	includedirs { "3rdparty/recast/Recast/include/" }