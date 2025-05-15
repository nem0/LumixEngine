# Non-programmers

You can get Lumix Engine from [itch.io](https://mikulasflorek.itch.io/lumix-engine) or download standalone zip file from [actions](https://github.com/nem0/LumixEngine/actions/workflows/deploy.yml).

# Programmers - Windows

## Prerequisites

* [Visual Studio 2022](https://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx) or compatible - MinGW is not supported

Download [bootstrap batch file]([../scripts/bootstrap.bat](https://github.com/nem0/LumixEngine/blob/master/scripts/bootstrap.bat)) and run it. The bootstrap file requires git in %PATH. It will fetch the repository, create Visual Studio solution and open it. After that you can build the solution and run the editor. If something fails or you do not want to use the batch file, [execute these 4 steps manually](../scripts/bootstrap.bat)

1. Download [source code](https://github.com/nem0/lumixengine).
2. Run `scripts\genie.exe vs2022`, it genereates VS solution.
3. Open VS solution `scripts\tmp\vs2022\LumixEngine.sln`.

[Video tutorial](https://www.youtube.com/watch?v=OjQKTA5ia2U)

# Making game in C++

1. Download Lumix Engine source code.
2. Run [scripts/plugins.bat](../scripts/plugins.bat).
3. Choose `Empty plugin template`.
4. Create a solution (e.g. run [scripts/create_vs22_sln.bat](../scripts/create_vs22_sln.bat)).
5. Put your game code in `plugins/myplugin/src`.
6. Build and run the project.


# Programmers - Linux

Linux is supported only up to [64b66124cb5e6d5bc8e7f11f129c0d5c63f2268a](https://github.com/nem0/LumixEngine/commit/64b66124cb5e6d5bc8e7f11f129c0d5c63f2268a) revision. Linux in newer versions is work in progress.

See [this shell script](https://raw.githubusercontent.com/wiki/nem0/LumixEngine/files/lumix_bootstrap.sh). It will fetch the repository, create makefile, build it and run the editor.

* Note: the shell script needs git, make and g++ accessible
* Note: to build using clang, call `./genie --gcc=linux-clang gmake`
* Note: to build using clang & ninja, call `./genie --gcc=linux-clang ninja`

[Video tutorial](https://www.youtube.com/watch?v=ic5ejjY6wZs)

# Programmers - Windows + Ninja + Clang

As an alternative to building with MSVC, Clang can be used. Since it's not the primary way to build the engine, it's a bit more involved. 

## Prerequisities

* [Clang](https://clang.llvm.org/)
* [Ninja](https://ninja-build.org/)

Both can be installed with Visual Studio installer or in any other way. Unlike when building with MSVC, there's no bootstrap script for clang, neither are there prebuilt 3rd party libraries. 

1. Download [source code](https://github.com/nem0/lumixengine).
2. Run [scripts/download_physx.bat](../scripts/download_physx.bat) to download Physx.
3. Run [scripts/download_luau.bat](../scripts/download_luau.bat) to Download Luau.
4. Run [scripts/download_freetype.bat](../scripts/download_freetype.bat) to Download FreeType2.
5. Run `scripts\genie.exe --gcc=windows-clang ninja`, it genereates ninja files in `tmp/ninja`.
6. Run `ninja` in `scripts\tmp\ninja\debug64` or `scripts\tmp\ninja\relwithdebinfo64`.
