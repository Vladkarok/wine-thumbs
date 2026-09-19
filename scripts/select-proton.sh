#!/bin/bash
# Point the launcher's proton-current link at the patched build of the
# installed GE-Proton, or at the stock GE-Proton while there is none; in that
# case a build is started in the background for the next launch.
#
# The link path stays the same either way, so Proton does not redo the
# prefix's DLL links when switching between the two.
#
# usage: select-proton.sh [--no-build] [link]
set -euo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

build=1
[ "${1:-}" = --no-build ] && { build=0; shift; }
link=${1:-$CURRENT_LINK}

tag=$(stock_tag)
patched=$(patched_proton_dir "$tag")

if [ -f "$patched/.wine-thumbs" ]; then
    target=$patched
else
    target=$STOCK_PROTON
    if [ "$build" = 1 ]; then
        mkdir -p "$LOG_DIR"
        setsid "$ROOT/scripts/update.sh" --background >> "$LOG_DIR/update.log" 2>&1 < /dev/null &
    fi
fi

[ "$(readlink "$link" 2>/dev/null)" = "$target" ] || ln -sfn "$target" "$link"
[ "$target" = "$STOCK_PROTON" ] || prune_patched_dirs "$target" > /dev/null
echo "$target"
