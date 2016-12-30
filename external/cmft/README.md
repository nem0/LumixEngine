[cmft](https://github.com/dariomanesku/cmft) - cubemap filtering tool
=====================================================================

Cross-platform open-source command-line cubemap filtering tool.

It reaches very fast processing speeds by utilizing both multi-core CPU and OpenCL GPU at the same time! (check [perfomance charts](https://github.com/dariomanesku/cmft#performance))

Download
--------

<img src="https://github.com/dariomanesku/cmft-bin/raw/master/res/windows.png" height="16px">  [cmft - Windows 64bit](https://github.com/dariomanesku/cmft-bin/raw/master/cmft_win64.zip)<br />
<img src="https://github.com/dariomanesku/cmft-bin/raw/master/res/linux.png"   height="16px">  [cmft - Linux 64bit](https://github.com/dariomanesku/cmft-bin/raw/master/cmft_lin64.zip) (updated: 16. Mar 2015)<br />
<img src="https://github.com/dariomanesku/cmft-bin/raw/master/res/apple.png"   height="16px">  [cmft - OSX 64bit](https://github.com/dariomanesku/cmft-bin/raw/master/cmft_osx64.zip) (updated: 16. Mar 2015)<br />
*In case you need 32bit binaries, compile from source.*<br />


![cmft-cover](https://github.com/dariomanesku/cmft/raw/master/res/cmft_cover.jpg)

- Supported input/output formats: \*.dds, \*.ktx, \*.hdr, \*.tga.
- Supported input/output types: cubemap, cube cross, latlong, face list, horizontal/vertical strip, octant.


See it in action - [here](https://github.com/dariomanesku/cmftStudio)
------------
Screenshot from [cmftStudio](https://github.com/dariomanesku/cmftStudio):
![cmftStudioScreenshot](https://raw.githubusercontent.com/dariomanesku/cmftStudio/master/screenshots/cmftStudio_osx0.jpg)


Remark !
--------

- If you are running OpenCL procesing on the same GPU that it connected to your monitor you may experience the following problem: when you are processing big cubemaps (~1024 face size) with small 'glossScale' parameter (<7 for example), OpenCL kernels may take a long time to execute and that may cause the operative system to step in and kill the display/gpu driver in the middle of processing! In case this happens, the processing will continue on CPU. You will get the expected results but the processing will run slower. To avoid this you can:
    - Use smaller input size. (this is crucial!)
    - Choose a bigger 'glossScale' parameter.
    - Use a workaround on Windows: increase the TdrDelay or modify the TdrLevel in the registry and restart the machine. More details here: http://msdn.microsoft.com/en-us/library/windows/hardware/ff569918%28v=vs.85%29.aspx
    - Run cmft on a faster GPU.
    - Run cmft on a GPU that is not connected to the monitor.


Building
--------

	git clone git://github.com/dariomanesku/cmft.git
	cd cmft
	make

- After calling `make`, *\_projects* folder will be created with all supported project files. Deleting *\_projects* folder is safe at any time.
- All compiler generated files will be in *\_build* folder. Again, deleting *\_build* folder is safe at any time.

### Windows

- Visual Studio
  - Visual Studio solution will be located in *\_projects/vs20XX/*.
- MinGW
  - MinGW Makefile will be located in *\_projects/gmake-mingw-gcc/*.
  - Project can be build from the root directory by running `make mingw-gcc-release64` (or similar).
- Remember to edit CMFT variable in *runtime/cmft_win.bat* accordingly to match the build configuration you are using.

### Linux

- Makefile will be loacted in *\_projects/gmake-linux/*.
- Project can be build from the root directory by running `make linux-release64` (or similar).
- Vim users can source *.ide.vim* and make use of Build() and Execute() functions from inside Vim.
- Remember to edit CMFT variable in *runtime/cmft_lin.sh* accordingly to match the build configuration you are using.

### OS X

- XCode
  - XCode solution will be located in *\_projects/xcode4/*.
  - XCode project contains one scheme with 4 build configurations (debug/release 32/64bit). Select desired build configuration manually and/or setup schemes manually as desired. In case you need 64bit build, it is possible to just set *Build Settings -> Architectures -> Standard Architectures (64-bit Intel) (x86_64).*
  - Also it is probably necessary to manually set runtime directory (it is not picking it from genie for some reason). This is done by going to "*Product -> Scheme -> Edit Scheme... -> Run cmftDebug -> Options -> Working Directory (Use custom working directory)*" and specifying *runtime/* directory from cmft root folder.
- Makefile
  - Makefile can be found in *\_projects/gmake-osx/*.
  - Project can be build from the root directory by running `make osx-release64` (or similar).
- Vim users can source *.ide.vim* and make use of Build() and Execute() functions from inside Vim.
- Remember to edit CMFT variable in *runtime/cmft_osx.sh* accordingly to match the build configuration you are using.

### Other
- Also other compilation options may be available, have a look inside *\_projects* directory.
- Additional build configurations will be available in the future. If one is there and not described here in this document, it is probably not yet set up properly and may not work out-of-the-box as expected without some care.

### Known issues
- Linux GCC build noticeably slower comparing to Windows build (haven't yet figured out why).
- PVRTexTool is not properly opening mipmapped \*.ktx files from cmft. This appears to be the problem with the current version of PVRTexTool. Has to be further investigated.


Performance
-----------

cmft was compared with the popular CubeMapGen tool for processing performance.
Test machine: Intel i5-3570 @ 3.8ghz, Nvidia GTX 560 Ti 448.

Filter settings:
- Gloss scale: 8
- Gloss bias: 1
- Mip count: 8
- Exclude base: false

Test case #1:
- Src face size: 256
- Dst face size: 256
- Lighting model: phongbrdf

Test case #2:
- Src face size: 256
- Dst face size: 256
- Lighting model: blinnbrdf

Test case #3:
- Src face size: 512
- Dst face size: 256
- Lighting model: phongbrdf

Test case #4:
- Src face size: 512
- Dst face size: 256
- Lighting model: blinnbrdf



|Test case| CubeMapGen   | cmft Cpu only | cmft Gpu only | cmft  |
|:--------|:-------------|:--------------|:--------------|:------|
|#1       |01:27.7       |00:08.6        |00:18.7        |00:06.0|
|#2       |04:39.5       |00:29.7        |00:19.6        |00:11.2|
|#3       |05:38.1       |00:33.4        |01:03.7        |00:21.6|
|#4       |18:34.1       |01:58.2        |01:07.7        |00:35.5|

![cmft-performance-chart](https://github.com/dariomanesku/cmft/raw/master/res/cmft_performance_chart.png)

*Notice: performance tests are outdated. cmft is now running noticeably faster than displayed!*


Environment maps
------------

- [NoEmotion HDRs](http://noemotionhdrs.net/).
- [sIBL Archive - Hdrlabs.com](http://www.hdrlabs.com/sibl/archive.html).


Recommended tools
------------

- [PVRTexTool](http://community.imgtec.com/developers/powervr/) - for opening \*.dds and \*.ktx files.
- [GIMP](http://www.gimp.org) - for opening \*.tga files.
- [Luminance HDR](http://qtpfsgui.sourceforge.net/) - for opening \*.hdr files.


Similar projects
------------

- [CubeMapGen](http://developer.amd.com/tools-and-sdks/archive/legacy-cpu-gpu-tools/cubemapgen/) - A well known tool for cubemap filtering from AMD.
- [Marmoset Skyshop](http://www.marmoset.co/skyshop) - Commercial plugin for Unity3D Game engine.
- [Knald Lys](https://www.knaldtech.com/lys-open-beta/) - Commercial tool from KnaldTech.


Useful links
------------

- [Sebastien Lagarde Blog - AMD Cubemapgen for physically based rendering](http://seblagarde.wordpress.com/2012/06/10/amd-cubemapgen-for-physically-based-rendering/) by [Sébastien Lagarde](https://twitter.com/SebLagarde)
- [The Witness Blog - Seamless Cube Map Filtering](http://the-witness.net/news/2012/02/seamless-cube-map-filtering/) by [Ignacio Castaño](https://twitter.com/castano)


Contribution
-----------

In case you are using cmft for your game/project, please let me know. Tell me your use case, what is working well and what is not. I will be happy to help you using cmft and also to fix possible bugs and extend cmft to match your use case.

Other than that, everyone is welcome to contribute to cmft by submitting bug reports, feature requests, testing on different platforms, profiling, etc.

When contributing to the cmft project you must agree to the BSD 2-clause licensing terms.

Contributors
------------

* Mmxix productions ([@mmxix](https://github.com/mmxix/)) - Vstrip image format.
* [Pierre Lepers](https://twitter.com/_pil_) ([@plepers](https://github.com/plepers)) - Octant image format.


Thanks to
------------

* [Marko Radak](http://markoradak.com/) - Initial cover photo design and realization.
* [Dorian Cioban](https://www.linkedin.com/in/doriancioban) - Additional cover photo improvements.


Contact
------------

Reach me via Twitter: [@dariomanesku](https://twitter.com/dariomanesku).


[License (BSD 2-clause)](https://github.com/dariomanesku/cmft/blob/master/LICENSE)
-------------------------------------------------------------------------------

    Copyright 2014-2015 Dario Manesku. All rights reserved.

    https://github.com/dariomanesku/cmft

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

       1. Redistributions of source code must retain the above copyright notice,
          this list of conditions and the following disclaimer.

       2. Redistributions in binary form must reproduce the above copyright notice,
          this list of conditions and the following disclaimer in the documentation
          and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDER ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
    EVENT SHALL COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
    ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
