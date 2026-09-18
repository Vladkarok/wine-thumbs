#!/bin/bash
# Assemble a patched GE-Proton: a copy of the installed one (reflinked where
# the filesystem allows, so it costs no space) with the patched DLLs in it.
# Copies that are no longer current are removed unless a game still runs
# from them.
#
# usage: install.sh
set -euo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

tag=$(stock_tag)
out=$ROOT/out/ge-$tag
dest=$(patched_proton_dir "$tag")

[ "$(cat "$out/TAG" 2>/dev/null)" = "$tag" ] || { echo "wine-thumbs: no build for $tag, run build.sh first" >&2; exit 1; }
[ "$(cat "$out/PATCH_HASH")" = "$(patch_hash)" ] || { echo "wine-thumbs: build of $tag is outdated, run build.sh" >&2; exit 1; }

if [ ! -f "$dest/.wine-thumbs" ]; then
    mkdir -p "$PATCHED_DIR"
    tmp=$PATCHED_DIR/.tmp-$tag
    rm -rf "$tmp"
    cp -a --reflink=auto "$STOCK_PROTON" "$tmp"
    for arch in "${PE_ARCHS[@]}"; do
        for dll in "${PATCHED_DLLS[@]}"; do
            install -m755 "$out/$arch-windows/$dll.dll" "$tmp/files/lib/wine/$arch-windows/$dll.dll"
        done
    done
    echo "$tag $(patch_hash)" > "$tmp/.wine-thumbs"
    rm -rf "$dest"
    mv "$tmp" "$dest"
    echo "wine-thumbs: installed $dest"
fi

for dir in "$PATCHED_DIR"/*/; do
    dir=${dir%/}
    [ "$dir" = "$dest" ] && continue
    if dir_in_use "$dir"; then
        echo "wine-thumbs: keeping $dir, still in use"
    else
        rm -rf "$dir"
        echo "wine-thumbs: removed $dir"
    fi
done
