# wine-thumbs: thumbnails in Wine file dialogs

Private patch set for Wine's `shell32` and `comdlg32`. It adds a Thumbnails view
to the Open/Save dialogs, so picking a screenshot in Last War (or any other
program in the prefix) shows real previews instead of a list of file names.

Wine's own dialog has no thumbnail support at all, and upstream Wine does not
accept LLM-written code, so this lives here as a local build.

![Stock GE-Proton dialog on the left, patched on the right](before-after.png)

## What it does

- A **Thumbnails** button (2x2 picture grid) next to List and Details in the
  classic dialog, and a **View > Thumbnails** item in the right-click menu of
  both the classic and the Vista-style (`IFileDialog`) dialog.
- The chosen view is remembered (`HKCU\Software\Wine\FileDialog\ViewMode`) and
  used by both dialog types next time.
- Image previews are decoded on up to 4 low-priority background threads, so the
  dialog never freezes. The items on screen are decoded first.
- Downscaling averages over the covered area. Wine's own WIC scaler only does
  nearest-neighbour, which turns screenshot text into noise.
- JPEG/TIFF EXIF rotation is applied. Small images are not enlarged. Previews
  sit on the label like in Explorer, and opaque ones get a thin frame.
- Every format Wine's WIC can decode works: PNG (8/16 bit, grey, palette,
  alpha), JPEG, BMP, GIF, TIFF, ICO, DDS, JPEG XR. The list comes from the
  installed decoders, so formats Wine adds later work without changes.
- Folders and other files get their shell icon rendered from the 256 px icons.
- Previews are cached per process (64 MB by default), so reopening the dialog
  is instant.
- Sizes follow the DPI. The Last War prefix runs at 192 DPI and the game is
  per-monitor aware, so previews are 192 px there.
- Also fixed: the dialog never showed which view button is active, because the
  toolbar's control id collides with the Open button's (patch 0003).

Settings, all optional, in the prefix registry:

| Key | Value | Default |
|---|---|---|
| `HKCU\Software\Wine\Thumbnails` | `Size` (DWORD, px at 96 DPI, 32 to 256) | 96 |
| `HKCU\Software\Wine\Thumbnails` | `CacheSize` (DWORD, MB, 0 = no cache) | 64 |
| `HKCU\Software\Wine\FileDialog` | `ViewMode` (DWORD, `FVM_*`) | set by the dialog |

Debug output: `WINEDEBUG=+thumbnail`.

## How it is wired into Last War

`~/.local/bin/lastwar-launch` calls `scripts/select-proton.sh` before starting
Proton. That script points `~/.local/share/lastwar/proton-current` at:

- `~/.local/share/lastwar/proton-thumbs/<GE tag>-<patch hash>/` if a patched
  build exists for the installed GE-Proton. This is a reflinked copy of the
  stock one plus the four patched DLLs, so it takes no extra disk space on btrfs.
- the stock `/usr/share/steam/compatibilitytools.d/proton-ge-custom` otherwise.
  It then starts `scripts/update.sh --background`, which builds the patched copy
  for the new version at low priority and sends a desktop notification. The
  next game start picks it up.

So a pacman update of `proton-ge-custom-bin` never leaves the game on an old
Proton. It runs stock for one session, then gets the thumbnails back. The link
path never changes, so Proton does not redo the prefix's DLL links when it
switches.

To turn it off for a run: `WINE_THUMBS=0 lastwar-launch`. To remove it: delete
the block marked "File dialog thumbnails" in `lastwar-launch` (a backup is in
`~/Work/lwupdate/lastwar-launch.bak-prethumbs-20260918`) and run
`ln -sfn /usr/share/steam/compatibilitytools.d/proton-ge-custom ~/.local/share/lastwar/proton-current`.

## Building by hand

```bash
scripts/update.sh            # build + install for the installed GE-Proton
scripts/build.sh GE-Proton11-7   # build only, any GE tag
```

`build.sh` reproduces GE's Wine tree exactly: it clones proton-ge-custom at the
tag with its `wine` and `wine-staging` submodules and runs the "WINE PATCHING"
section of GE's own `protonprep-valve-staging.sh` (`scripts/ge-prep-wine.sh`),
so everything Valve, staging and GE changed in these DLLs is kept. The result
is committed and tagged `baseline`. Every build then resets to `baseline`,
applies `patches/*.patch` with `git am` (falling back to `patch -F3` for
context drift), appends the translations and builds only the two DLLs, for
x86_64 and i386. The first build of a new tag takes a few minutes, mostly
cloning. Rebuilds take about 10 seconds.

Logs: `logs/update.log`, `src/ge-<tag>/prep.log`, `build/ge-<tag>/make.log`.

## If a new GE release breaks the patches

`update.sh` then notifies that the build failed, and the game keeps running on
stock GE-Proton. It retries at most every 6 hours. The patches have to be
rebased on the new tree: fix them in `src/ge-<tag>/wine` and re-export with
`git format-patch baseline..HEAD -o patches/`.

The same series applies without changes to GE-Proton11-6 and 11-7 (Wine 11.0
+ Valve + staging), upstream Wine 11.17 and upstream master. It builds without
warnings on GE-Proton11-6, 11-7 and upstream master, and is tested on 11-7.

## Patches

1. `shell32: Add a thumbnail view mode.` All the thumbnail logic is in the new
   `dlls/shell32/shlview_thumbs.c`. `shlview.c` only gets small hooks: one
   view-mode switch function, custom draw, and no image index in thumbnail mode.
2. `shell32: Keep the view mode when the ExplorerBrowser navigates.`
3. `comdlg32: Send toolbar control messages to the toolbar.` An existing Wine
   bug: `IDC_TOOLBAR == IDOK`.
4. `comdlg32: Add a thumbnails view button and remember the view mode.`

Menu and tooltip strings are translated for 33 of Wine's languages, including
Ukrainian and Russian, via `translations/thumbnails.tsv`. They are appended to
`po/*.po` at build time, so upstream changes to the po files never conflict.

## Tests

- `test/unit/run.sh`: the resampler against an exact area-average reference on
  3000 random sizes. Max error 0.5, i.e. rounding only.
- `test/thumbtest.c` + `test/run-test.sh`: opens the real dialog headless
  (Wine null display driver), scripts it (switch view, select, scroll, change
  folder, close mid-decode) and saves PNG captures to `test/out/`.

Setup, after `scripts/build.sh`:

```bash
test/setup-runtime.sh        # test copy of GE-Proton + headless prefix in runtime/
test/make-images.sh          # test image folders (ImageMagick)
test/build-test.sh           # thumbtest.exe; "i386" for thumbtest32.exe
test/run-test.sh 'wait:800,cmd:0xA006,wait:2000,shot:thumbs,close'
```

`run-test.sh` takes `--32`, `--itemdlg` and `--dpiaware`. The actions are
listed at the top of `thumbtest.c`. After changing the patches, rebuild and
run `test/install-test-dlls.sh`.

Known and unrelated: Wine's classic file dialog leaks about 40 GDI handles per
open, with or without these patches.
