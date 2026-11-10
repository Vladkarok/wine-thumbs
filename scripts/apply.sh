#!/bin/bash
# Apply the patch series (patches/series) to a Wine source tree, one patch at
# a time, and say which patch fails first.
#
# Each patch is first tried as is (git apply --check). If that fails and the
# tree is a git repository, a 3-way merge is tried (git am -3), which copes
# with context that moved. In a git repository every patch becomes a commit,
# so after fixing a conflict you can carry on with `git am --continue` and
# then re-export with scripts/export-patches.sh.
#
# usage: apply.sh [--check] [--base REV] [--no-po] <wine source dir>
#   --check     try the series on a throwaway copy and leave the tree as it
#               is; for a git tree that copy is a temporary worktree of the
#               committed state, so uncommitted changes are ignored
#   --base REV  with --check on a git tree: start from REV instead of HEAD,
#               e.g. "baseline" in a tree that scripts/build.sh prepared
#   --no-po     do not append the translations to po/*.po afterwards
set -euo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

check=0 base= po=1
while [ $# -gt 0 ]; do
    case "$1" in
    --check) check=1 ;;
    --base) base=${2:?--base needs a revision}; shift ;;
    --no-po) po=0 ;;
    -h|--help) sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    -*) echo "apply.sh: unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
    shift
done
[ $# = 1 ] || { echo "usage: apply.sh [--check] [--base REV] [--no-po] <wine source dir>" >&2; exit 2; }
tree=$(realpath "$1")
[ -f "$tree/dlls/shell32/shlview.c" ] || { echo "apply.sh: $tree is not a Wine source tree" >&2; exit 2; }
[ -z "$base" ] || [ "$check" = 1 ] || { echo "apply.sh: --base only works with --check" >&2; exit 2; }

mapfile -t patches < <(patch_list)
[ "${#patches[@]}" -gt 0 ] || { echo "apply.sh: no patches in $PATCH_DIR" >&2; exit 2; }

# a tree that is not its own git repository must not be mistaken for part
# of a repository above it
is_git=0
if [ "$(git -C "$tree" rev-parse --show-toplevel 2>/dev/null)" = "$tree" ]; then
    is_git=1
else
    export GIT_CEILING_DIRECTORIES=$(dirname "$tree")
fi
[ -z "$base" ] || [ "$is_git" = 1 ] || { echo "apply.sh: --base needs a git tree" >&2; exit 2; }

if [ "$check" = 1 ]; then
    # git trees: a temporary worktree at REV (default HEAD), so uncommitted
    # changes are not part of the check; other trees: a reflinked copy
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/wine-thumbs-check.XXXXXX")
    orig=$tree
    tree=$tmp/wine
    if [ "$is_git" = 1 ]; then
        trap 'git -C "$orig" worktree remove --force "$tree" > /dev/null 2>&1; rm -rf "$tmp"; git -C "$orig" worktree prune' EXIT
        git -C "$orig" worktree add -q --detach "$tree" "${base:-HEAD}"
    else
        trap 'rm -rf "$tmp"' EXIT
        cp -a --reflink=auto "$orig" "$tree"
    fi
    po=0
fi
g() { git -C "$tree" "$@"; }

if [ "$is_git" = 1 ]; then
    # git am needs a committer; the patch keeps its own author
    [ -n "$(g config user.email || true)" ] || g() { git -C "$tree" -c user.name=wine-thumbs -c user.email=wine-thumbs@localhost "$@"; }
    [ -d "$(g rev-parse --path-format=absolute --git-path rebase-apply)" ] && { echo "apply.sh: a git am is already in progress in $tree" >&2; exit 1; }
fi

n=0 merged=0
for p in "${patches[@]}"; do
    n=$((n + 1))
    name=$(basename "$p")
    [ -f "$p" ] || { echo "apply.sh: $name is in the series but missing" >&2; exit 2; }
    if g apply --check "$p" 2> /dev/null; then
        how=clean
        if [ "$is_git" = 1 ]; then g am -q "$p"; else g apply "$p"; fi
    elif [ "$is_git" = 1 ] && g am -q -3 "$p" > /dev/null 2>&1; then
        how="3-way merge"
        merged=$((merged + 1))
    else
        echo "FAIL  $name"
        echo
        echo "Patch $n of ${#patches[@]} does not apply$( [ "$n" = 1 ] || echo ", the ones before it do")."
        if [ "$check" = 1 ]; then
            echo "Run without --check in a git tree to get the conflict markers."
        elif [ "$is_git" = 1 ]; then
            echo "git am stopped with conflict markers in $tree. Resolve them, then:"
            echo "  git -C $tree add <files> && git -C $tree am --continue"
            [ "$n" -lt "${#patches[@]}" ] && echo "  git -C $tree am -3 ${patches[*]:$n}"
            echo "  $ROOT/scripts/export-patches.sh $tree"
            echo "Or give up with: git -C $tree am --abort"
        else
            echo "This tree is not a git repository, so no 3-way merge was possible."
            echo "See what fails with: git -C $tree apply --reject $p"
        fi
        exit 1
    fi
    printf 'ok    %s (%s)\n' "$name" "$how"
done

if [ "$po" = 1 ]; then
    "$ROOT/scripts/apply-translations.sh" "$tree"
    echo "translations appended to po/*.po (not committed)"
fi
msg="all ${#patches[@]} patches apply"
[ "$merged" = 0 ] || msg="$msg, $merged of them only with a 3-way merge (re-export them)"
echo "$msg"
