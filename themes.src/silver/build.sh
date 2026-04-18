rm -rf *.pak
../../makeobj/makeobj pak silver.pak standard.dat
../../makeobj/makeobj pak silver-large.pak standard-large.dat
mv *.pak ../../../../simutrans/themes
cp *.tab ../../../../simutrans/themes

