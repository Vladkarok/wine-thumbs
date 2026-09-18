#!/bin/bash
# Build the resampler from shlview_thumbs.c natively and compare it against
# an exact area-average reference on random sizes.
set -e
cd "$(dirname "$0")"
src=${1:-../../src/ge-GE-Proton11-7/wine/dlls/shell32/shlview_thumbs.c}
{ cat header.c.in; sed -n '/^struct resampler$/,/^\/\* EXIF orientation/p' "$src" | sed '$d'; cat main.c.in; } > resample_test.c
gcc -O2 -std=gnu99 -o resample_test resample_test.c -lm
./resample_test
