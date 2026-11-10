#!/bin/bash
# Build patched shell32.dll and comdlg32.dll for a GE-Proton release.
#
# The Wine tree is reproduced exactly as GE builds it (Valve's wine plus the
# staging and GE patches, via GE's own prep script), so nothing GE changed in
# these DLLs is lost. Output goes to out/ge-<tag>/<arch>-windows/.
#
# usage: build.sh [GE tag]   (default: the installed GE-Proton)
set -euo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

tag=${1:-$(stock_tag)}
src=$SRC_DIR/ge-$tag
wine=$src/wine
bld=$BUILD_DIR/ge-$tag
out=$OUT_DIR/ge-$tag
mkdir -p "$LOG_DIR"

step() { echo "wine-thumbs: $tag: $*"; }
die() { echo "wine-thumbs: $tag: $*" >&2; exit 1; }

# 1. pristine GE wine tree, prepared once per release and tagged "baseline"
if [ ! -f "$src/.prepared" ]; then
    step "fetching GE sources"
    rm -rf "$src"
    git clone -q --depth 1 --branch "$tag" "$GE_REPO" "$src"
    git -C "$src" submodule update -q --init --depth 1 wine wine-staging

    # commits reverted by the prep script are not in a shallow clone
    prep=$src/patches/protonprep-valve-staging.sh
    for commit in $(sed -n '/^### (2) WINE PATCHING ###/,/^### END WINE PATCHING ###/p' "$prep" \
            | grep -oE 'git revert [^#]*' | grep -oE '[0-9a-f]{7,40}'); do
        git -C "$wine" fetch -q --depth 2 origin "$commit"
    done

    step "applying GE patches (log: $src/prep.log)"
    "$ROOT/scripts/ge-prep-wine.sh" "$src" > "$src/prep.log" 2>&1 || die "GE prep failed, see $src/prep.log"

    git -C "$wine" config user.name wine-thumbs
    git -C "$wine" config user.email wine-thumbs@localhost
    git -C "$wine" add -A
    git -C "$wine" commit -q --no-verify -m "$tag prepared tree"
    git -C "$wine" tag -f baseline > /dev/null
    touch "$src/.prepared"
fi

# 2. our patches on top, from scratch every time
step "applying thumbnail patches"
git -C "$wine" am --abort > /dev/null 2>&1 || true
git -C "$wine" reset -q --hard baseline
git -C "$wine" clean -q -fd
mapfile -t patches < <(patch_list)
if ! git -C "$wine" am -q "${patches[@]}" 2> "$LOG_DIR/am-$tag.log"; then
    # newer trees may have moved the context around; accept some fuzz
    git -C "$wine" am --abort
    for p in "${patches[@]}"; do
        patch -d "$wine" -p1 -F3 -s --no-backup-if-mismatch < "$p" \
            || die "$(basename "$p") does not apply to $tag, the patches need a rebase"
    done
fi
"$ROOT/scripts/apply-translations.sh" "$wine"

# 3. configure once, then build just the two DLLs
mkdir -p "$bld"
if [ ! -f "$bld/Makefile" ]; then
    step "configuring (log: $bld/configure.log)"
    (cd "$bld" && "$wine/configure" "${WINE_CONFIGURE_ARGS[@]}" > configure.log 2>&1) \
        || die "configure failed, see $bld/configure.log"
fi

targets=()
for arch in "${PE_ARCHS[@]}"; do
    for dll in "${PATCHED_DLLS[@]}"; do targets+=("dlls/$dll/$arch-windows/$dll.dll"); done
done
step "building (log: $bld/make.log)"
make -C "$bld" -j"$(nproc)" "${targets[@]}" > "$bld/make.log" 2>&1 || die "build failed, see $bld/make.log"

# 4. stage
rm -rf "$out"
for arch in "${PE_ARCHS[@]}"; do
    mkdir -p "$out/$arch-windows"
    for dll in "${PATCHED_DLLS[@]}"; do
        install -m644 "$bld/dlls/$dll/$arch-windows/$dll.dll" "$out/$arch-windows/$dll.dll"
    done
done
patch_hash > "$out/PATCH_HASH"
echo "$tag" > "$out/TAG"
step "built $out"
