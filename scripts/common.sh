# Shared settings for the wine-thumbs scripts (sourced, not executed).

ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")
# the GE-Proton installed by the proton-ge-custom-bin package
STOCK_PROTON=${WINE_THUMBS_STOCK_PROTON:-/usr/share/steam/compatibilitytools.d/proton-ge-custom}
# patched copies of it, one per GE version and patch set
PATCHED_DIR=${WINE_THUMBS_PROTON_DIR:-$HOME/.local/share/lastwar/proton-thumbs}
LOG_DIR=$ROOT/logs
GE_REPO=https://github.com/GloriousEggroll/proton-ge-custom.git

# configure only what is needed to build PE DLLs
WINE_CONFIGURE_ARGS=(--enable-archs=i386,x86_64 --disable-tests
    --without-x --without-wayland --without-freetype --without-gstreamer --without-ffmpeg
    --without-pulse --without-alsa --without-oss --without-cups --without-sane --without-gphoto
    --without-v4l2 --without-pcap --without-netapi --without-krb5 --without-gssapi --without-opencl
    --without-usb --without-vulkan --without-sdl --without-dbus --without-udev --without-unwind
    --without-capi --without-opengl --without-osmesa --without-inotify --without-gnutls)
PATCHED_DLLS=(shell32 comdlg32)
PE_ARCHS=(x86_64 i386)

# "1789520217 GE-Proton11-7" -> GE-Proton11-7
stock_tag() { awk 'NR == 1 {print $NF}' "$STOCK_PROTON/version"; }

# identifies the patch set, so a changed patch triggers a rebuild
patch_hash() { cat "$ROOT"/patches/*.patch "$ROOT/translations/thumbnails.tsv" | sha256sum | cut -c1-12; }

patched_proton_dir() { echo "$PATCHED_DIR/$1-$(patch_hash)"; }

# true while a Wine process runs from the directory
dir_in_use() { grep -qsF "$1/" /proc/[0-9]*/maps; }

# the launcher's link, which the prefix's DLL links go through
CURRENT_LINK=${WINE_THUMBS_LINK:-$HOME/.local/share/lastwar/proton-current}

# remove patched copies other than $1 that no game runs from and that the
# launcher's link does not point at
prune_patched_dirs() {
    local dir linked
    linked=$(readlink -f "$CURRENT_LINK" 2>/dev/null)
    for dir in "$PATCHED_DIR"/*/; do
        dir=${dir%/}
        [ -d "$dir" ] && [ "$dir" != "$1" ] && [ "$dir" != "$linked" ] || continue
        if dir_in_use "$dir"; then
            echo "wine-thumbs: keeping $dir, still in use"
        else
            rm -rf "$dir"
            echo "wine-thumbs: removed $dir"
        fi
    done
}

notify() {
    echo "wine-thumbs: $*"
    command -v notify-send >/dev/null && notify-send -a wine-thumbs "Эскизы в диалоге файлов" "$*" || true
}
