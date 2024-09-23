On Windows all 3rd party libraries are vendored in the LumixEngine repository, either as a binary, or in form of a source code. Everything should work out of the box.

## Recast

We utilize [Recast & Detour](https://github.com/recastnavigation/recastnavigation) for navigation purposes. The source code for Recast is included in our repository under [external/recast/src](../external/recast/src). To update Recast, you can run the provided [batch script](../scripts/download_deploy_recast.bat) which will download the latest version and copy it to the appropriate directory. Recast is included as a [unity build](https://en.wikipedia.org/wiki/Unity_build). Recast is build as a part of *navigation* plugin.

## FreeType2

We utilize [FreeType2](https://github.com/nem0/freetype2.git) for text rendering. On Windows, prebuilt static library is included in [external/freetype/lib/win](../external/freetype/lib/win/) and is used by default. If you want to build FreeType from source code, use following steps:
1. Run [download_freetype.bat](../scripts/download_freetype.bat) to download FreeType source code.
2. Regenerate project using *GENie*.
3. Build project.

## Luau

We utilize [Luau](https://github.com/luau-lang/luau) for scripting. On Windows, prebuilt *dynamic* library is included in [external/luau/lib/win](../external/luau/lib/win/) and is used by default. We use dynamic library because it's much smaller (~100x on Windows) than a static library. If you want to build Luau from source code, use following steps:
1. Run [download_luau.bat](../scripts/download_luau.bat) to download Luau source code.
2. Regenerate project using *GENie*.
3. Build project.

If you build Luau from source code like this, it's built as a static library by default. To build a dynamic library, pass `--luau-dynamic` to GENie. Static library version of Luau includes Luau analysis, which is used by Studio to report type errors in Luau scripts. Dynamic library does not include this analysis.