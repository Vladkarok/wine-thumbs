#!/bin/bash
# Build the resampler from shlview_thumbs.c natively and compare it against
# an exact area-average reference on random sizes.
# usage: run.sh [path to a patched dlls/shell32/shlview_thumbs.c]
#   default: the source tree of the installed GE-Proton (or WINE_THUMBS_TAG)
set -e
here=$(dirname "$(realpath "$0")")
if [ -n "${1:-}" ]; then
    src=$(realpath "$1")
else
    . "$here/../../scripts/common.sh"
    src=$SRC_DIR/ge-${WINE_THUMBS_TAG:-$(stock_tag)}/wine/dlls/shell32/shlview_thumbs.c
fi
cd "$here"
{ cat header.c.in; sed -n '/^struct resampler$/,/^\/\* EXIF orientation/p' "$src" | sed '$d'; cat main.c.in; } > resample_test.c
gcc -O2 -std=gnu99 -o resample_test resample_test.c -lm
./resample_test
