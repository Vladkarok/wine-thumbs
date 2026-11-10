#!/bin/bash
# Regenerate patches/ and patches/series from a patched Wine git tree: every
# commit after BASE becomes one patch, in order. Commit messages are the
# patch descriptions, so edit them there (git rebase -i, git commit --amend).
#
# usage: export-patches.sh <wine git tree> [BASE]
#   BASE defaults to "baseline", the tag scripts/build.sh puts on the
#   prepared GE tree. For an upstream Wine tree pass the commit you started
#   from, e.g. wine-11.0.
#
# Note: the patch hash changes with the files, so the next game launch
# rebuilds the patched GE-Proton (see scripts/select-proton.sh).
set -euo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: export-patches.sh <wine git tree> [BASE]" >&2; exit 2; }
tree=$(realpath "$1")
base=${2:-baseline}
[ "$(git -C "$tree" rev-parse --show-toplevel 2>/dev/null)" = "$tree" ] \
    || { echo "export-patches.sh: $tree is not the top of a git tree" >&2; exit 2; }
count=$(git -C "$tree" rev-list --count "$base..HEAD")
[ "$count" -gt 0 ] || { echo "export-patches.sh: no commits after $base" >&2; exit 1; }
[ -z "$(git -C "$tree" status --porcelain --untracked-files=no -- dlls include)" ] \
    || echo "export-patches.sh: warning: uncommitted changes under dlls/ or include/ are not exported" >&2

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$tree" format-patch -q --no-signature --zero-commit -o "$tmp" "$base..HEAD"

rm -f "$PATCH_DIR"/*.patch
mv "$tmp"/*.patch "$PATCH_DIR/"
{
    echo '# Apply in this order. scripts/apply.sh and scripts/build.sh read this'
    echo '# file, scripts/export-patches.sh rewrites it.'
    for p in "$PATCH_DIR"/*.patch; do basename "$p"; done
} > "$PATCH_DIR/series"
echo "exported $count patches from $tree ($base..HEAD) to $PATCH_DIR"
