#!/bin/sh

# clean any previous data
rm -rvf data

# create folder
mkdir -v data

# copy executable
cp -v ../../dist/p2000t-cartridge-studio.exe data/

# run windeploy
/c/msys64/mingw64/bin/windeployqt-qt5.exe data/p2000t-cartridge-studio.exe --release --force

# copy icon
cp -v ../assets/icon/p2000t-cartridge-studio.ico data/

# copy licence
cp -v ../../LICENSE data/license.txt

# build installer script
/C/Users/iawfi/anaconda3/python.exe package.py

# create installer
/c/Program\ Files\ \(x86\)/Inno\ Setup\ 6/ISCC.exe setup.iss
