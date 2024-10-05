On Windows, all 3rd party libraries are vendored in the LumixEngine repository, either as a binary or in the form of source code. Everything should work out of the box.

## Dear ImGui

We utilize [Dear ImGui](https://github.com/ocornut/imgui) for user interface of the editor. The source code for Dear ImGui is includede in our repository under [external/imgui](../external/imgui/). We use docking branch. To update Dear ImGui, you can run the provided [batch script](../scripts/update_imgui.bat). Dear ImGui is included as a [unity build](https://en.wikipedia.org/wiki/Unity_build).

## Recast

We utilize [Recast & Detour](https://github.com/recastnavigation/recastnavigation) for navigation purposes. The source code for Recast is included in our repository under [external/recast/src](../external/recast/src). To update Recast, you can run the provided [batch script](../scripts/download_deploy_recast.bat), which will download the latest version and copy it to the appropriate directory. Recast is included as a [unity build](https://en.wikipedia.org/wiki/Unity_build). Recast is built as a part of the *navigation* plugin.

## FreeType2

We utilize [FreeType2](https://github.com/nem0/freetype2.git) for text rendering. On Windows, the prebuilt static library is included in [external/freetype/lib/win](../external/freetype/lib/win/) and is used by default. If you want to build FreeType from source code, use the following steps:
1. Run [download_freetype.bat](../scripts/download_freetype.bat) to download the FreeType source code.
2. Regenerate the project using *GENie*.
3. Build the project.

## Luau

We utilize [Luau](https://github.com/luau-lang/luau) for scripting. On Windows, the prebuilt *dynamic* library is included in [external/luau/lib/win](../external/luau/lib/win/) and is used by default. We use the dynamic library because it's much smaller (~100x on Windows) than a static library. If you want to build Luau from source code, use the following steps:
1. Run [download_luau.bat](../scripts/download_luau.bat) to download the Luau source code.
2. Regenerate the project using *GENie*.
3. Build the project.

If you build Luau from source code like this, it's built as a static library by default. To build a dynamic library, pass `--luau-dynamic` to GENie. The static library version of Luau includes Luau analysis, which is used by Studio to report type errors in Luau scripts. The dynamic library does not include this analysis.

## PhysX

We utilize [PhysX](https://github.com/nem0/PhysX.git) for physical simulation. On Windows, the prebuilt static library is included in [external/physx/lib/win](../external/physx/lib/win) and is used by default. If you want to build PhysX from source code, use the following steps:

1. Run [download_physx.bat](../scripts/download_physx.bat) to download the PhysX source code.
2. Regenerate the project using *GENie*.
3. Build the project.

## LZ4

We utilize [LZ4](https://github.com/lz4/lz4) for asset compression due to its exceptional speed and efficient compression ratio.  The source code for LZ4 is includede in our repository under [external/lz4](../external/lz4/).

## meshoptimizer

We utilize [meshoptimizer](https://github.com/zeux/meshoptimizer) for automatic LOD generation. The source code for meshoptimizer is included in our repository under [external/meshoptimizer](../external/meshoptimizer/). 

## MikkTSpace
We utilize [MikkTSpace](https://github.com/mmikk/MikkTSpace) for generating tangent space as it is the de-facto standard. The source code for MikkTSpace is included in our repository under [external/mikktspace](../external/mikktspace/).

## OpenFBX

We utilize [OpenFBX](https://github.com/nem0/OpenFBX/) for loading FBX files due to its high quality and open-source nature. The source code for OpenFBX is included in our repository under [external/openfbx](../external/openfbx/).

## rgbcx

We utilize [rgbcx](https://github.com/richgel999/bc7enc) for efficient BC texture compression due to its best-in-class speed and compression quality. The source code for rgbcx is included in our repository under [external/rgbcx](../external/rgbcx/).

## stb

We utilize [stb](https://github.com/nothings/stb) for image manipulation and loading Vorbis files. The source code for stb is included in our repository under [external/stb](../external/stb/).

## xxHash

We utilize [xxHash](https://github.com/Cyan4973/xxHash) for its exceptional speed and hash quality. The source code for xxHash is included in our repository under [external/xxhash](../external/xxhash/).