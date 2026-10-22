#!/bin/sh
# Maintainer script (Linux + llvm-mingw): builds Install/Uninstall .exe around a built plugin DLL.
# usage: build_windows.sh /path/to/llvm-mingw /path/to/plugin.dll /output/dir
set -e
L=$1; DLL=$2; OUT=$3; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; cd "$OUT"
sed "s#@PLUGIN_DLL@#$DLL#" "$HERE/installer.rc.in" > installer.rc; cp "$HERE/installer.manifest" "$HERE/uninstaller.rc" .
$L/bin/x86_64-w64-mingw32-windres installer.rc -O coff -o inst.res.o
$L/bin/x86_64-w64-mingw32-windres uninstaller.rc -O coff -o uninst.res.o
$L/bin/x86_64-w64-mingw32-clang++ -O2 -std=c++17 -municode -mwindows -static -o Install-DumbDockAmp.exe "$HERE/installer.cpp" inst.res.o -lshell32 -luser32
$L/bin/x86_64-w64-mingw32-clang++ -O2 -std=c++17 -municode -mwindows -static -DUNINSTALL -o Uninstall-DumbDockAmp.exe "$HERE/installer.cpp" uninst.res.o -lshell32 -luser32
