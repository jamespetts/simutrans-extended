rm -rf *.pak
../../makeobj/makeobj pak purple_night.pak standard.dat
../../makeobj/makeobj pak purple_night-large.pak standard-large.dat
mv *.pak ../../../../simutrans/themes
cp *.tab ../../../../simutrans/themes

