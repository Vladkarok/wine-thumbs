#!/bin/bash
# Copy freshly built DLLs into the test Proton copy.
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
. "$root/scripts/common.sh"
tag=${WINE_THUMBS_TAG:-$(stock_tag)}
for arch in "${PE_ARCHS[@]}"; do
    for dll in "${PATCHED_DLLS[@]}"; do
        install -m755 "$BUILD_DIR/ge-$tag/dlls/$dll/$arch-windows/$dll.dll" \
            "$root/runtime/$tag/files/lib/wine/$arch-windows/$dll.dll"
    done
done
echo "installed test DLLs into runtime/$tag"
