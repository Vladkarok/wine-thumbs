#!/bin/bash
# Build thumbtest.exe (or thumbtest32.exe) against the Wine build tree of the
# installed GE-Proton (or WINE_THUMBS_TAG), which scripts/build.sh creates.
# usage: build-test.sh [x86_64|i386]
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
. "$root/scripts/common.sh"
tag=${WINE_THUMBS_TAG:-$(stock_tag)}
arch=${1:-x86_64}
bld=$BUILD_DIR/ge-$tag
src=$SRC_DIR/ge-$tag/wine
exe=$root/test/thumbtest.exe
[ "$arch" = i386 ] && exe=$root/test/thumbtest32.exe

libs=(libs/uuid libs/compiler-rt dlls/comdlg32 dlls/ole32 dlls/shell32 dlls/user32 dlls/gdi32
      dlls/ucrtbase dlls/msvcrt dlls/winecrt0 dlls/kernel32 dlls/ntdll)
targets=() ldirs=()
for lib in "${libs[@]}"; do
    targets+=("$lib/$arch-windows/lib${lib##*/}.a")
    ldirs+=("-L$lib/$arch-windows")
done
make -C "$bld" -j"$(nproc)" "${targets[@]}" include/wincodec.h include/shobjidl.h > /dev/null

cd "$bld"
./tools/winegcc/winegcc -b "$arch-windows" --wine-objdir . -I"$src/include" -I"$src/include/msvcrt" -Iinclude \
    -D__WINE_PE_BUILD -mconsole -municode -o "$exe" "$root/test/thumbtest.c" "${ldirs[@]}" \
    -lcomdlg32 -lole32 -lshell32 -luuid -luser32 -lgdi32 -lucrtbase -lkernel32 -lcompiler-rt
echo "built $exe"
