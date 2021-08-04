#!/bin/bash

#
# This file is part of the Simutrans-Extended project under the Artistic License.
# (see LICENSE.txt)
#

#
# parameters (in this order):
# "-no-lang" prevents downloading the translations
# "-no-rev" do not include revision number in zip file name
# "-rev=###" overide SDL revision with ## (number)


# get pthreads DLL
getDLL()
{
	# Use curl if available, else use wget
	echo "Downloading pthreadGC2.dll"
	curl -q -h > /dev/null
	if [ $? -eq 0 ]; then
		curl -q ftp://sourceware.org/pub/pthreads-win32/dll-latest/dll/x86/pthreadGC2.dll > pthreadGC2.dll || {
		echo "Error: download of PthreadGC2.dll failed (curl returned $?)" >&2
		exit 4
	}
	else
		wget -q --help > /dev/null
		if [ $? -eq 0 ]; then
			wget -q -N ftp://sourceware.org/pub/pthreads-win32/dll-latest/dll/x86/pthreadGC2.dll || {
			echo "Error: download of PthreadGC2.dll failed (wget returned $?)" >&2
			exit 4
		}
		else
			echo "Error: Neither curl or wget are available on your system, please install either and try again!" >&2
			exit 6
		fi
	fi
}

getSDL2mac()
{
	# Use curl if available, else use wget
	curl -h > /dev/null
	if [ $? -eq 0 ]; then
		curl http://www.libsdl.org/release/SDL2-2.0.10.dmg > SDL2-2.0.10.dmg || {
			echo "Error: download of file SDL2-2.0.10.dmg failed (curl returned $?)" >&2
			rm -f "SDL2-2.0.10.dmg"
			exit 4
		}
		curl http://www.libsdl.org/release/SDL2-2.0.10.dmg > SDL2-2.0.10.dmg || {
			echo "Error: download of file SDL2-2.0.10.dmg failed (curl returned $?)" >&2
			rm -f "SDL2-2.0.10.dmg"
			exit 4
		}
	else
		wget --help > /dev/null
		if [ $? -eq 0 ]; then
			wget -N  http://www.libsdl.org/release/SDL2-2.0.10.dmg || {
				echo "Error: download of file SDL2-2.0.10.dmg failed (wget returned $?)" >&2
				rm -f "SDL2-2.0.10.dmg"
				exit 4
			}
			wget -N http://www.libsdl.org/release/SDL2-2.0.10.dmg || {
				echo "Error: download of file SDL2-2.0.10.dmg failed (wget returned $?)" >&2
				rm -f "SDL2-2.0.10.dmg"
				exit 4
			}
		else
			echo "Error: Neither curl or wget are available on your system, please install either and try again!" >&2
			exit 6
		fi
	fi
}


# (otherwise there will be many .svn included under windows)
distribute()
{
	# pack all files of the current release
	FILELISTE=$(find simutrans -type f "(" -name "*.tab" -o -name "*.mid" -o -name "*.bdf" -o -name "*.fnt" -o -name "*.txt"  -o -name "*.dll" -o -name "*.pak" -o -name "*.nut" -o -name "*.dll" ")")
	zip -9 $simarchiv.zip $FILELISTE simutrans/$simexe_name simutrans/$updater 1>/dev/null
}


buildOSX()
{
  echo "Build Mac OS package"
	# builds a bundle for MAC OS
	mkdir -p "simutrans-extended.app/Contents/MacOS"
	mkdir -p "simutrans-extended.app/Contents/Resources"
	cp $BUILDDIR$simexe_name   "simutrans-extended.app/Contents/MacOS/$simexe_name"
	strip "simutrans-extended.app/Contents/MacOS/$simexe_name"
	cp "../OSX/simutrans.icns" "simutrans-extended.app/Contents/Resources/simutrans-extended.icns"
	localostype=`uname -o`
	if [ "Msys" == "$localostype" ] || [ "Linux" == "$localostype" ]; then
		# only 7z on linux and windows can do that ...
		getSDL2mac
		7z x "SDL2-2.0.10.dmg"
		rm SDL2-2.0.10.dmg
	else
		# assume MacOS
		mkdir -p "simutrans-extended.app/Contents/Frameworks/"
		# add those paths if you want to ship fluidsynth
			#"/usr/local/opt/glib/lib/libgthread-2.0.0.dylib" \
			#"/usr/local/opt/fluid-synth/lib/libfluidsynth.2.dylib" \
		cp "/usr/local/opt/freetype/lib/libfreetype.6.dylib" \
			"/usr/local/opt/libpng/lib/libpng16.16.dylib" \
			"/usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib" \
			"simutrans-extended.app/Contents/Frameworks/"
		install_name_tool -change "/usr/local/opt/freetype/lib/libfreetype.6.dylib" "@executable_path/../Frameworks/libfreetype.6.dylib" "simutrans-extended.app/Contents/MacOS/$simexe_name"
		install_name_tool -change "/usr/local/opt/libpng/lib/libpng16.16.dylib" "@executable_path/../Frameworks/libpng16.16.dylib" "simutrans-extended.app/Contents/MacOS/$simexe_name"
		sudo install_name_tool -change "/usr/local/opt/libpng/lib/libpng16.16.dylib" "@executable_path/../Frameworks/libpng16.16.dylib" "simutrans-extended.app/Contents/MacOS/$simexe_name"
		install_name_tool -change "/usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib" "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" "simutrans-extended.app/Contents/MacOS/$simexe_name"
		#uncomment for fluidsynth
		#install_name_tool -change "/usr/local/opt/glib/lib/libgthread-2.0.0.dylib" "@executable_path/../Frameworks/libgthread-2.0.0.dylib" "simutrans.app/Contents/MacOS/simutrans"
		#install_name_tool -change "/usr/local/opt/fluid-synth/lib/libfluidsynth.2.dylib" "@executable_path/../Frameworks/libfluidsynth.2.dylib" "simutrans.app/Contents/MacOS/simutrans"
	fi
	echo "APPL????" > "simutrans-extended.app/Contents/PkgInfo"
	sh ../OSX/plistgen.sh "simutrans-extended.app" "$simexe_name"
}



# first assume unix name defaults ...
updatepath="/"
updater="get_pak.sh"

# now get the OSTYPE from config.default and remove all spaces around
OST=$(grep "^OSTYPE" config.default | sed "s/OSTYPE[ ]*=[ ]*//" | sed "s/[ ]*\#.*//")

# now get the OSTYPE from config.default and remove all spaces around
PGC=$(grep "^BUNDLE_PTHREADGC2" config.default | sed "s/BUNDLE_PTHREADGC2[ ]*=[ ]*//" | sed "s/[ ]*\#.*//")

if [ -n $PCG ]; then
	PGC=0
fi


BUILDDIR="$(grep '^PROGDIR' config.default | sed 's/PROGDIR[ ]*=[ ]*//' | sed 's/[ ]*\#.*//')"

if [ -n "$BUILDDIR" ]; then
	BUILDDIR="$(pwd)/"
else
	BUILDDIR="$(pwd)/build/default"
fi

# now make the correct archive name
simexe_name=simutrans-extended

if [ "$OST" = "mac" ]; then
	simarchivbase=simumac
elif [ "$OST" = "haiku" ]; then
	simarchivbase=simuhaiku
elif [ "$OST" = "mingw" ]; then
	simexe_name="$(simexe_name).exe"
	SDLTEST=$(grep "^BACKEND[ ]*=" config.default | sed "s/BACKEND[ ]*=[ ]*//" | sed "s/[ ]*\#.*//")
	if [ "$SDLTEST" = "sdl" ]  ||  [ "$SDLTEST" = "sdl2" ]; then
		simarchivbase=simuwin-sdl
	else
		simarchivbase=simuwin
		# Missing: Copy matching SDL dll!
	fi

	cd simutrans
		if [ $PGC -ne 0 ]; then
			getDLL
		fi
	cd ..

	updatepath="/nsis/"
	updater="download-paksets.exe"

	cd nsis
		makensis onlineupgrade.nsi
	cd ..

elif [ "$OST" = "linux" ]; then
	simarchivbase=simulinux
elif [ "$OST" = "freebsd" ]; then
	simarchivbase=simubsd
elif [ "$OST" = "amiga" ]; then
	simarchivbase=simuamiga
fi

# Test if there is something to distribute ...
if [ ! -f "$BUILDDIR/$simexe_name" ]; then
	echo "No simutrans executable found at '$BUILDDIR/$simexe'! Aborted!"
	exit 1
fi

# now add revision number without any modificators
# fetch language files
if [ "$#" = "0"  ]; then
	# try local answer assuming we use svn
	REV_NR=$(git rev-parse --short HEAD)
	simarchiv=$simarchivbase-$REV_NR
elif [ `expr match "$*" ".*-rev="` > 0 ]; then
	REV_NR=$(echo $* | sed "s/.*-rev=[ ]*//" | sed "s/[^0-9]*//")
	simarchiv=$simarchivbase-$REV_NR
else
	echo "No revision given!"
	simarchiv=$simarchivbase
fi

echo "Targeting archive $simarchiv"

# now build the archive for distribution
cd simutrans

if [ "$OST" = "mac" ]; then
    buildOSX
    cd ..
    ls
    pwd
    zip -r -9 - simutrans > simumac.zip
    cd simutrans
    rm -rf simutrans-extended.app
    exit 0
else
    echo "Building default zip archive..."
    cp "$BUILDDIR/$simexe_name" ./$simexe_name
    strip ./$simexe_name
    cp ..$updatepath$updater $updater
	cd ..

	distribute

	# .. finally delete executable and language files
	rm simutrans/$simexe_name
fi

echo "Created archive $simarchiv.zip"

# cleanup dll's
if [ $PGC -ne 0 ]; then
	rm simutrans/pthreadGC2.dll
fi

# swallow any error values, return success in any case
exit 0
