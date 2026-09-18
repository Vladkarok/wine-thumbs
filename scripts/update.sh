#!/bin/bash
# Build and install the patched GE-Proton for the installed GE version, then
# point the launcher's proton-current link at it.
#
# usage: update.sh [--background]
#   --background  low priority, desktop notifications, and no retry for six
#                 hours after a failure (used by select-proton.sh)
set -uo pipefail
. "$(dirname "$(realpath "$0")")/common.sh"

background=0
[ "${1:-}" = --background ] && background=1
tag=$(stock_tag)
mkdir -p "$LOG_DIR"
failed=$LOG_DIR/failed-$tag-$(patch_hash)

exec 9> "$LOG_DIR/update.lock"
if ! flock -n 9; then
    echo "wine-thumbs: an update is already running"
    exit 0
fi

if [ "$background" = 1 ]; then
    [ -n "$(find "$failed" -mmin -360 2>/dev/null)" ] && exit 0
    renice -n 19 $$ > /dev/null
    ionice -c 3 -p $$ 2> /dev/null || true
fi

if "$ROOT/scripts/build.sh" "$tag" && "$ROOT/scripts/install.sh"; then
    rm -f "$failed"
    # sources and builds of other GE releases are ~1 GB each
    for dir in "$ROOT"/src/ge-* "$ROOT"/build/ge-* "$ROOT"/out/ge-*; do
        [ -e "$dir" ] && [ "${dir##*/}" != "ge-$tag" ] && rm -rf "$dir"
    done
    "$ROOT/scripts/select-proton.sh" --no-build
    [ "$background" = 1 ] && notify "Собрано для $tag. Эскизы появятся при следующем запуске игры."
    exit 0
fi

touch "$failed"
[ "$background" = 1 ] && notify "Не удалось собрать для $tag, игра пока идёт на стоковом Proton. Лог: $LOG_DIR/update.log"
exit 1
