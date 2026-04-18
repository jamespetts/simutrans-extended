#!/bin/bash

#
# This file is part of the Simutrans-Extended project under the Artistic License.
# (see LICENSE.txt)
#

# assumes makeobj is in ../build/default/makeobj-extended

cd aero
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak aerotheme.pak skins_aero.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../flat
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak flat.pak flat-skin.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../standard
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak classic.pak standard.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../highcontrast
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak highcontrast.pak theme.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../highcontrast-large
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak highcontrast-large.pak theme.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../modern
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak modern.pak standard.dat
../../build/default/makeobj-extended/makeobj-extended pak modern-large.pak standard-large.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../newstandard
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak newstandard.pak newstandard.dat
../../build/default/makeobj-extended/makeobj-extended pak newstandard-large.pak newstandard-large.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../silver
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak silver.pak standard.dat
../../build/default/makeobj-extended/makeobj-extended pak silver-large.pak standard-large.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes

cd ../purple_night
rm -rf *.pak
../../build/default/makeobj-extended/makeobj-extended pak purple_night.pak standard.dat
../../build/default/makeobj-extended/makeobj-extended pak purple_night-large.pak standard-large.dat
mv *.pak ../../simutrans/themes
cp *.tab ../../simutrans/themes
