#!/bin/bash
# Create the headless test runtime: runtime/<tag>, a copy of the installed
# GE-Proton with the test DLLs, and runtime/testcompat, a prefix that uses
# Wine's null display driver so dialogs render without showing up on screen.
# Run scripts/build.sh first.
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
. "$root/scripts/common.sh"
tag=${WINE_THUMBS_TAG:-$(stock_tag)}
rt=$root/runtime
mkdir -p "$rt"
rm -rf "${rt:?}/$tag"
cp -a --reflink=auto "$STOCK_PROTON" "$rt/$tag"

run() { env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$rt/testcompat/pfx" WINEDEBUG=-all "$@"; }
if [ ! -d "$rt/testcompat/pfx" ]; then
    mkdir -p "$rt/testcompat"
    env -u DISPLAY -u WAYLAND_DISPLAY STEAM_COMPAT_DATA_PATH="$rt/testcompat" \
        STEAM_COMPAT_CLIENT_INSTALL_PATH=/usr/share/steam PROTON_USE_XALIA=0 \
        "$rt/$tag/proton" run cmd.exe /c exit > /dev/null 2>&1 || true
fi
run "$rt/$tag/files/bin/wine" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d null /f > /dev/null
run "$rt/$tag/files/bin/wineserver" -w
"$root/test/install-test-dlls.sh"
