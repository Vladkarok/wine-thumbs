#!/bin/bash
# Run thumbtest.exe headless (Wine null display driver) in the test prefix
# with the test Proton copy; see setup-runtime.sh.
# usage: run-test.sh [--32] [--itemdlg] [--dpiaware] <actions> [dir]
#   env: TEST_LC_ALL (locale), REPEAT (dialog rounds), WINEDEBUG
set -u
root=$(realpath "$(dirname "$0")/..")
. "$root/scripts/common.sh"
tag=${WINE_THUMBS_TAG:-$(stock_tag)}
exe=$root/test/thumbtest.exe mode=
while :; do
    case "${1:-}" in
    --32) exe=$root/test/thumbtest32.exe; shift ;;
    --*=*|--save|--ansi|--multi) mode="$mode $1"; shift ;;
    --itemdlg|--dpiaware) mode="$mode $1"; shift ;;
    *) break ;;
    esac
done
actions=$1; dir=${2:-$root/test/images}
mkdir -p "$root/test/out"
winepath() { echo "Z:${1//\//\\}"; }
env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$root/runtime/testcompat/pfx" WINEDEBUG=${WINEDEBUG:--all} \
    WINEDLLOVERRIDES="winegstreamer=" LC_ALL=${TEST_LC_ALL:-C.UTF-8} \
    timeout 90 "$root/runtime/$tag/files/bin/wine" "$exe" $mode \
    "$(winepath "$dir")" "$(winepath "$root/test/out")" "$actions" ${REPEAT:-1}
echo "exit=$?"
