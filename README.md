# Simutrans-Extended

- 1.0) [About](#1-about)
	- 1.1) [Download Simutrans-Extended](#11-download-simutrans-extended)
	- 1.2) [Helpful links](#12-helpful-links)
- 2.0) [Compiling Simutrans-Extended](#2-compiling-simutrans-extended)
	- 2.1) [Getting the Source Code](#21-getting-the-source-code)
	- 2.2) [Getting the libraries](#22-getting-the-libraries)
	- 2.3) [Compiling](#23-compiling)
		- 2.3.1) [Compiling with make](#232-compiling-with-make)
		- 2.3.2) [Compiling with MSVC](#233-compiling-with-microsoft-visual-studio)
		- 2.3.3) [Compiling with CMake](#231-compiling-with-cmake)
	- 2.4) [Cross-Compiling](#24-Cross-Compiling)
- 3.0) [Contribute](#3-contribute)
	- 3.1) [Coding](#31-coding)
	- 3.2) [Translating](#32-translating)
	- 3.3) [Painting](#33-painting)
	- 3.4) [Reporting-bugs](#34-reporting-bugs)
- 4.0) [License](#4-license)
- 5.0) [Credits](#5-credits)

## 1) About

Simutrans-Extended (formerly Simutrans-Experimental) is a fork of the popular open source transport simulation game, Simutrans. Your goal is to establish a successful transport company. Transport passengers, mail and goods by rail, road, ship, and even air. Interconnect districts, cities, public buildings, industries and tourist attractions by building a transport network you always dreamed of.

## 1.1) Download Simutrans-Extended

You can download Simutrans-Extended from:

- [Bridgewater-Brunel Simutrans-Extended server](http://bridgewater-brunel.me.uk/downloads/nightly/)
- [Steam](https://store.steampowered.com/app/434520/Simutrans/) (opting in the "extended" beta branch)

Simutrans-Extended is updated nightly, so you will need an up-to-date game to play network games. For this purpose, an [automatic updater is available](http://bridgewater-brunel.me.uk/downloads/Nightly%20Updater%20V2.zip) (requires Java 9 or later). If you play Simutrans-Extended on Steam, the Steam client will take care of updates for you.

## 1.2) Helpful links

- [Simutrans-Extended International Sub-Forum](https://forum.simutrans.com/index.php#c19)
- [Simutrans-Extended Wiki](https://simutrans-germany.com/wiki/wiki/en_extended_Index)
- [Overview of Simutrans-Extended features](https://forum.simutrans.com/index.php?topic=1959.0)
- [Simutrans-Extended gameplay help requests](https://forum.simutrans.com/index.php/board,154.0.html)

## 2) Compiling Simutrans-Extended

This is a short guide on compiling Simutrans-Extended. If you want more detailed information, read the [Compiling Simutrans](https://simutrans-germany.com/wiki/wiki/en_CompilingSimutrans) wiki page. Note: A system set up to compile Simutrans should be able to compile Simutrans-Extended.

If you are on Windows, download either [Microsoft Visual Studio](https://visualstudio.microsoft.com/) or [MSYS2](https://www.msys2.org/). MSVC is easy for debugging, MSYS2 is easy to set up (but it has to be done on the command line).

### 2.1) Getting the Source Code

You can download the latest version with a git client:
```
git clone https://github.com/jamespetts/simutrans-extended.git
```

### 2.2) Getting the libraries

This is a list of libraries used by Simutrans-Extended. Not all of them are necessary, some are optional, so pick them according to your needs. Read below about how to install them.

| Library       | Website                             | Necessary? | Notes                                                                    |
|---------------|-------------------------------------|------------|---------------------------------------------------------------------------|
| zlib          | https://zlib.net/                   | Necessary  | Basic compression support                                                 |
| bzip2         | https://www.bzip.org/downloads.html | Necessary  | Alternative compression. You can pick this or zstd                         |
| libpng        | http://www.libpng.org/pub/png/      | Necessary  | Image manipulation                                                         |
| libSDL2       | http://www.libsdl.org/              | Necessary* | *On Linux & Mac. Optional but recommended for Windows. Graphics back-end |
| libzstd       | https://github.com/facebook/zstd    | Optional   | Alternative compression (larger save files than bzip2, but faster)         |
| libfreetype   | http://www.freetype.org/            | Optional   | TrueType font support                                                     |
| libminiupnpc  | http://miniupnp.free.fr/            | Optional   | Easy Server option                                                         |
| libfluidsynth | https://www.fluidsynth.org/         | Optional   | MIDI playback recommended on Linux & temporarily on Mac                   |
| libSDL2_mixer | http://www.libsdl.org/              | Optional   | Alternative MIDI playback and sound system                                 |

You will also need pkgconfig (Unix) or [vcpkg](https://github.com/Microsoft/vcpkg) (MSVC + CMake)

- MSVC + CMake: Install libaries using vpkg `./vcpkg/vcpkg --triplet x64-windows-static install zlib bzip2 libpng zstd sdl2 freetype miniupnpc`
- MSVC (alone): You will need to download libraries manually. Check [How to compile Simutrans Extended with Visual Studio 2015](https://forum.simutrans.com/index.php/topic,14254.msg194481.html#msg194481) for more info.
- MSYS2: Run [setup-mingw.sh](https://github.com/aburch/simutrans/blob/master/tools/setup-mingw.sh) to get the libraries and set up the environment.
- Linux: Use [pkgs.org](https://pkgs.org/) to search for development libraries available in your package manager.
- Mac: Install libraries via [Homebrew](https://brew.sh/).

### 2.3) Compiling

Go to the source code directory of simutrans-extended. You have three build systems to choose from: make, MSVC, and CMake. We recommend make or MSVC for debug builds.

Compiling will give you only the executable, you still need a Simutrans-Extended installation to run the program. You can start simutrans-extended with `-use_workdir` to point it to an existing installation.

#### 2.3.1) Compiling with make

The executable will be built in build/default.

```
(Linux) autoconf
(MacOS) autoreconf -ivf
./configure
make -j 4
(MacOS) make OSX/getversion
```

#### 2.3.2) Compiling with Microsoft Visual Studio

Open [Simutrans-Extended.sln](./Simutrans-Extended.sln), choose the configuration you want and compile.

#### 2.3.3) Compiling with CMake

The executable will be built in build/simutrans-extended.

##### Linux/MinGW/MacOS
```
mkdir build && cd build
cmake -G "Insert Correct Makefiles Generator" ..
cmake --build . -j 4
```
See [here](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html) for a list of generators.

##### MSVC
```
mkdir build && cd build
cmake.exe .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### 2.4) Cross-Compiling

If you want to cross-compile Simutrans-Extended from Linux for Windows, see the [Cross-Compiling Simutrans](https://simutrans-germany.com/wiki/wiki/en_Cross-Compiling_Simutrans) wiki page.

## 3) Contribute
You cand find general information about contributing to Simutrans-Extended in the [Development Index](https://simutrans-germany.com/wiki/wiki/en_Devel_Index?structure=en_Devel_Index) of the wiki.

### 3.1) Coding

- If you want to contribute, read the coding guidelines in simutrans/documentation/coding_styles.txt
- You definitely should check out the [Technical Documentation Sub-Forum](https://forum.simutrans.com/index.php/board,112.0.html) as well.
- Pull Requests in GitHub are welcome, but you should use the [Simutrans-Extended development](https://forum.simutrans.com/index.php/board,53.0.html) Sub-Forum to discuss your changes.

### 3.2) Translating

Simutrans-Extended is constantly updating and adding texts so we are always in need for translators:

- To help with translation use the [SimuTranslator](https://translator.simutrans.com/) web tool.
- To request a translator account use the [Translation Sub-Forum](https://forum.simutrans.com/index.php/board,47.0.html).

### 3.3) Painting

Simutrans-Extended is always looking for artists! If you want to paint graphics for Simutrans-Extended, check:

- The "Creating images" section of the [Development Index](https://simutrans-germany.com/wiki/wiki/en_Devel_Index).
- The [General Resources and Tools](https://forum.simutrans.com/index.php/board,108.0.html) Sub-Forum.

### 3.4) Reporting bugs

Please make sure you are using the latest version of Simutrans-Extended before reporting any bugs. For bug reports use the [Bug Reports](https://forum.simutrans.com/index.php/board,152.0.html) Sub-Forum.

## 4) License

Simutrans-Extended is licensed under the Artistic License version 1.0. The Artistic License 1.0 is an OSI-approved license which allows for use, distribution, modification, and distribution of modified versions, under the terms of the Artistic License 1.0. For the complete license text see [LICENSE.txt](./LICENSE.txt).

Simutrans-Extended paksets (which are necessary to run the game) have their own license, but no one is included alongside this code.

## 5) Credits

Simutrans was originally written by Hansjörg Malthaner "Hajo" from 1997 until he retired from development around 2004. Since then a team of contributors (The Simutrans Team) lead by Markus Pristovsek "Prissi" develop Simutrans. The Simutrans-Extended development is lead by James E. Petts with help of many contributors.

A list of early contributors can be found in [simutrans/thanks.txt](./simutrans/thanks.txt)
