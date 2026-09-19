# wine-thumbs: a Windows-like file dialog for Wine

Private patch set for Wine's `shell32` and `comdlg32`. Programs that call
`GetOpenFileName`/`GetSaveFileName`, like Last War when you attach a picture,
get the Vista-style dialog as on Windows, with image previews, an address box,
and a sort order and folder that are remembered.

Upstream Wine does not accept LLM-written code, so this lives here as a local
build.

![Stock GE-Proton dialog on the left, patched on the right](before-after.png)

## What it does

**The dialog**
- `GetOpenFileName`/`GetSaveFileName` show the item (`IFileDialog`) dialog, as
  Windows has done since Vista, unless the program customizes the dialog with
  a hook, a template or a help button. That includes multiple selection, the
  ANSI calls, the "Open as read-only" choice in the Open drop-down button, and
  `FNERR_BUFFERTOOSMALL`.
- An address box shows the current folder. Type a path there, absolute or
  relative to the current folder, and press Enter to go to it.
- New folder and View buttons (Thumbnails / List / Details).
- The dialog opens at the size you last gave it.
- It opens in the folder you last picked a file from. Windows 7 does this too
  when the program passes the same initial folder as the first time. Last War
  always passes the same one.

**Thumbnails**
- A Thumbnails view in both dialogs: the toolbar button in the classic one, the
  View button in the new one, and View > Thumbnails in the right-click menu.
- Previews are decoded on up to 4 low-priority background threads, so the
  dialog never freezes. What is on screen gets decoded first.
- Downscaling averages over the covered area. Wine's own WIC scaler only does
  nearest-neighbour, which turns screenshot text into noise. JPEG/TIFF EXIF
  rotation is applied, and small images are not enlarged.
- Every format Wine's WIC can decode works: PNG (8/16 bit, grey, palette,
  alpha), JPEG, BMP, GIF, TIFF, ICO, DDS, JPEG XR. The list comes from the
  installed decoders, so formats Wine adds later work without changes.
- Previews are cached per process (64 MB by default), so reopening is instant.
- Sizes follow the DPI. The Last War prefix runs at 192 DPI and the game is
  per-monitor aware, so previews are 192 px there.

**Sorting and menus**
- The sort order is remembered per folder, in every view. Sort by date once in
  the screenshots folder and it stays newest first.
- Right-click > Arrange Icons offers By Name / Type / Size / Date plus
  Ascending / Descending. Date and size start newest and biggest first, like
  Explorer, and names sort naturally ("2" before "10"). The details header shows
  the sort arrow.
- The right-click menu of the file list lost its View, Arrange Icons and New
  submenus in Wine: an id mix-up dropped them when the menu was merged. They are
  back, with the current view and sort marked.
- The classic dialog never showed which view button is active: its toolbar
  shares the control id of the Open button.

Settings, all optional, in the prefix registry:

| Key | Value | |
|---|---|---|
| `HKCU\Software\Wine\FileDialog` | `Classic` (DWORD 1) | keep the old dialog; also per program in `AppDefaults\<exe>\FileDialog` |
| `HKCU\Software\Wine\FileDialog` | `ViewMode` (DWORD, `FVM_*`) | written by the dialogs |
| `HKCU\Software\Wine\FileDialog` | `ItemDialogSize` | written by the new dialog |
| `HKCU\Software\Wine\FileDialog\FirstFolder` | per program | the first initial folder it passed |
| `HKCU\Software\Wine\Shell\FolderSort` | per folder | column, direction; the last 500 folders |
| `HKCU\Software\Wine\Thumbnails` | `Size` (DWORD, px at 96 DPI, 32 to 256) | default 96 |
| `HKCU\Software\Wine\Thumbnails` | `CacheSize` (DWORD, MB, 0 = none) | default 64 |

Debug output: `WINEDEBUG=+thumbnail,+commdlg`.

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
Proton. It runs stock for one session, then gets the patches back. The link
path never changes, so Proton does not redo the prefix's DLL links when it
switches. Old copies are deleted once no game runs from them and the link no
longer points at them.

To turn it off for a run: `WINE_THUMBS=0 lastwar-launch`. To remove it: delete
the block marked "File dialog thumbnails" in `lastwar-launch` (a backup is in
`~/Work/lwupdate/lastwar-launch.bak-prethumbs-20260918`) and run
`ln -sfn /usr/share/steam/compatibilitytools.d/proton-ge-custom ~/.local/share/lastwar/proton-current`.

## Building by hand

```bash
scripts/update.sh            # build + install for the installed GE-Proton
scripts/build.sh GE-Proton11-7   # build only, any GE tag
```

`build.sh` reproduces GE's Wine tree exactly. It clones proton-ge-custom at the
tag with its `wine` and `wine-staging` submodules, then runs the "WINE PATCHING"
section of GE's own `protonprep-valve-staging.sh` through
`scripts/ge-prep-wine.sh`, the way GE's build runs it. That keeps everything
Valve, staging and GE changed in these DLLs. The result is committed and tagged
`baseline`. Every build then resets to `baseline`, applies `patches/*.patch`
with `git am` (falling back to `patch -F3` for context drift), appends the
translations and builds only the two DLLs, for x86_64 and i386. The first build
of a new tag takes a few minutes, mostly cloning. Rebuilds take about 10 seconds.

Logs: `logs/update.log`, `src/ge-<tag>/prep.log`, `build/ge-<tag>/make.log`.

## If a new GE release breaks the patches

`update.sh` then notifies that the build failed, and the game keeps running on
stock GE-Proton. It retries at most every 6 hours. The patches have to be
rebased on the new tree: fix them in `src/ge-<tag>/wine` and re-export with
`git format-patch baseline..HEAD -o patches/`.

The series applies without changes to GE-Proton11-7 (Wine 11.0 + Valve +
staging), upstream Wine 11.17 and upstream master. It builds without warnings on
GE-Proton11-7 and upstream master and is tested on GE-Proton11-7. The build
pipeline itself was also run from scratch on GE-Proton11-6.

## Patches

1. `shell32: Add a thumbnail view mode.` All the thumbnail logic is in the new
   `dlls/shell32/shlview_thumbs.c`. `shlview.c` only gets small hooks.
2. `shell32: Keep the view mode when the ExplorerBrowser navigates.`
3. `comdlg32: Send toolbar control messages to the toolbar.` Existing Wine bug:
   `IDC_TOOLBAR == IDOK`.
4. `comdlg32: Add a thumbnails view button and remember the view mode.`
5. `shell32: Fix the background menu and remember the sort order of each folder.`
   Persistence lives in `dlls/shell32/shlview_sort.c`.
6. `comdlg32: Use the item dialog for GetOpenFileName and GetSaveFileName.`
   The mapping lives in `dlls/comdlg32/filedlg_item.c`.
7. `comdlg32: Add an address box and New folder and View buttons to the item dialog.`

Menu and tooltip strings are translated for 33 of Wine's languages, including
Ukrainian and Russian, via `translations/thumbnails.tsv`. They are appended to
`po/*.po` at build time, so upstream changes to the po files never conflict.
The new dialog's Open/read-only items and the View tooltip reuse strings Wine
already translates.

## Tests

- `test/unit/run.sh`: the resampler against an exact area-average reference on
  3000 random sizes. Max error 0.5, i.e. rounding only.
- `test/thumbtest.c` + `test/run-test.sh`: opens the real dialog headless
  (Wine null display driver), scripts it and saves PNG captures to `test/out/`.
  It can switch views, run context-menu commands, select, type a name and press
  Open, use the address box, click toolbar buttons, resize, and print the
  returned `OPENFILENAME` fields.

Setup, after `scripts/build.sh`:

```bash
test/setup-runtime.sh        # test copy of GE-Proton + headless prefix in runtime/
test/make-images.sh          # test image folders (ImageMagick)
test/build-test.sh           # thumbtest.exe; "i386" for thumbtest32.exe
test/run-test.sh 'wait:1500,menucmd:0x2d,wait:2000,shot:thumbs,type:rose.bmp,ok'
```

`run-test.sh` takes `--32`, `--itemdlg`, `--dpiaware`, `--save`, `--ansi`,
`--multi`, `--flags=`, `--defext=`, `--file=` and `--maxfile=`. The actions are
listed at the top of `thumbtest.c`. After changing the patches, rebuild and run
`test/install-test-dlls.sh`.

Known and unrelated: Wine's classic file dialog leaks about 40 GDI handles per
open, with or without these patches.
