#!/bin/bash
# Reproduce GE-Proton's patched Wine tree by running the "WINE PATCHING"
# section of GE's own patches/protonprep-valve-staging.sh, with GE's own
# patch helpers, the way GE's build runs it: patch failures are not fatal
# there either, and the .orig backups patch leaves behind are relied on by
# later GE patches. Failures are only reported, the build decides.
#
# usage: ge-prep-wine.sh <proton-ge-custom checkout with wine + wine-staging submodules>
set -euo pipefail
ge_dir=$(realpath "$1")
prep="$ge_dir/patches/protonprep-valve-staging.sh"
section=$(mktemp)
trap 'rm -f "$section"' EXIT

{
    # helper functions defined above the first section
    sed -n '1,/^### (1) PREP SECTION ###/p' "$prep"
    sed -n '/^### (2) WINE PATCHING ###/,/^### END WINE PATCHING ###/p' "$prep"
} > "$section"

grep -q '^apply_patch()' "$section" || { echo "apply_patch() not found in $prep" >&2; exit 1; }
grep -q 'pushd wine' "$section" || { echo "WINE PATCHING section not found in $prep" >&2; exit 1; }

cd "$ge_dir"
bash "$section" < /dev/null 2>&1 | tee "$ge_dir/prep-section.log"
failures=$(grep -cE '^[0-9]+ out of [0-9]+ hunks? (FAILED|ignored)|can.t find file to patch' "$ge_dir/prep-section.log" || true)
[ "$failures" = 0 ] || echo "ge-prep-wine: $failures patch hunks did not apply (GE's build ignores them too)" >&2

# Proton's Makefile.in ".wine-post-source" step: generated sources that the
# Valve tree no longer carries.
cd "$ge_dir/wine"
XDG_CACHE_HOME=$(mktemp -d) dlls/winevulkan/make_vulkan -x vk.xml -X video.xml
tools/make_specfiles || true
