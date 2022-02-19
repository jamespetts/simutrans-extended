About
=====

Simutrans-Extended (formerly Simutrans-Experimental) is a fork of the popular open source transport simulation game, Simutrans. Your goal is to establish a successful transport company. Transport passengers, mail and goods by rail, road, ship, and even air. Interconnect districts, cities, public buildings, industries and tourist attractions by building a transport network you always dreamed of.

You can download Simutrans-Extended from:

- Bridgewater-Brunel Simutrans-Extended server:   http://bridgewater-brunel.me.uk/downloads/nightly/
- Steam (opting in the "extended" beta branch):  https://store.steampowered.com/app/434520/Simutrans/

Check the forum or wiki if you need help:

- International Forum: https://forum.simutrans.com/index.php#c19
- Simutrans Wiki:      https://simutrans-germany.com/wiki/wiki/en_extended_Index


Compiling Simutrans-Extended
============================

This is a short guide on compiling simutrans. If you want more detailed information, read the Compiling Simutrans wiki page https://simutrans-germany.com/wiki/wiki/en_CompilingSimutrans

Note: A system set up to compile Simutrans should be able to compile Simutrans-Extended.

If you are on Windows, download either MSVC or MinGW. MSVC is easy for debugging, MSYS2 is easy to set up (but it has to be done on the command line).
- MSVC:  https://visualstudio.microsoft.com/
- MSYS2: https://www.msys2.org/ 

Getting the libraries
------------------------

You will need pkgconfig (Unix) or vcpkg (Microsoft Visual C++) https://github.com/Microsoft/vcpkg

- Needed (All): libpng2 libbzip2 zlib 
- Needed (Linux/Mac): libSDL2 libfluidsynth (for midi music)
- Optional but recommended: libzstd (faster compression) libfreetype (TrueType font support) miniupnpc (for easy server setup)

- MSVC + CMake: Install libaries using vpkg "./vcpkg/vcpkg --triplet x64-windows-static install zlib bzip2 libpng zstd sdl2 freetype miniupnpc"
- MSVC (alone): You will need to download libraries manually. Check https://forum.simutrans.com/index.php/topic,14254.msg194481.html#msg194481 for more info.
- MSYS2: Run https://github.com/aburch/simutrans/blob/master/tools/setup-mingw.sh to get the libraries and set up the environment.
- Linux: Use https://pkgs.org/ to search for development libraries available in your package manager.
- Mac: Install libraries via Homebrew: https://brew.sh/

Compiling
---------

Go to the source code directory of simutrans-extended. You have three build systems to choose from: make, MSVC, and CMake. We recommend make or MSVC for debug builds.

Compiling will give you only the executable, you still need a Simutrans installation to run the program. You can start simutrans-extended with "-use_workdir" to point it to an existing installation.

1) make

(MacOS) autoreconf -ivf
(Linux) autoconf
./configure
make -j 4
(MacOS) make OSX/getversion

2) Microsoft Visual C++

Open Simutrans-Extended.sln, choose the configuration you want and compile.

3) CMake

mkdir build && cd build
(Unix) cmake -G "Insert Correct Makefiles Generator" ..
(Unix) cmake build . -j 4
(MSVC) cmake.exe .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
(MSVC) cmake.exe --build . --config Release

See here a list of generators: https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html


Cross-Compiling
===============
If you want to cross-compile Simutrans-Extended from Linux for Windows, see the Cross-Compiling Simutrans wiki page https://simutrans-germany.com/wiki/wiki/en_Cross-Compiling_Simutrans


Contribute
==========

You cand find general information about contributing to Simutrans-Extended in the Development Index of the wiki: https://simutrans-germany.com/wiki/wiki/en_Devel_Index?structure=en_Devel_Index


Reporting bugs
==============

For bug reports use the Bug Reports Sub-Forum: https://forum.simutrans.com/index.php/board,8.0.html


License
=======

Simutrans-Extended is licensed under the Artistic License version 1.0. The Artistic License 1.0 is an OSI-approved license which allows for use, distribution, modification, and distribution of modified versions, under the terms of the Artistic License 1.0. For the complete license text see LICENSE.txt.

Simutrans-Extended paksets (which are necessary to run the game) have their own license, but no one is included alongside this code.


Credits
=======

Simutrans was originally written by Hansjörg Malthaner "Hajo" from 1997 until he retired from development around 2004. Since then a team of contributors (The Simutrans Team) lead by Markus Pristovsek "Prissi" develop Simutrans. The Simutrans-Extended development is lead by James E. Petts with help of many contributors.

A list of early contributors can be found in simutrans/thanks.txt
