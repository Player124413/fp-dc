# Building the Dreamcast disc from your copy of Freedom Planet

The project contains no game data. The builder reads **your** Freedom Planet and makes the Dreamcast disc image.

## Quick start

You need Freedom Planet **1.21.5**: the Windows version from GOG (what most people have), or the Linux version.

**Windows (10 or 11, 64-bit)**

1. Download this project (on GitHub: *Code* > *Download ZIP*) and unzip it, for example to `C:\fp-dc`.
2. Open your Freedom Planet folder (GOG: *More* > *Manage installation* > *Show folder*, usually
   `C:\GOG Games\Freedom Planet`). Drag its **`Assets.dat`** onto **`build_dreamcast.bat`**. (Or unzip the project
   inside your Freedom Planet folder and double-click `build_dreamcast.bat`: an `Assets.dat` in the project folder or up
   to three folders above it, or in the usual Steam / GOG folders, is found by itself; otherwise a file dialog asks.)
3. A window shows the progress. At the end it says `Finished`: the disc image is **`fpdc.cdi`**, next to
   `build_dreamcast.bat`.

**Linux (x86_64)**

    ./build_dreamcast.sh "/path/to/Freedom Planet/Assets.dat"

The disc image is `fpdc.cdi`, next to the script. (Without an argument it looks for the Assets.dat as on Windows, then
asks with a file dialog when zenity is installed.)

| | Windows | Linux |
|---|---|---|
| first run downloads | about 110 MB, once | about 55 MB, once |
| build time | 20-60 minutes, depending on the computer (most of it: the 57 levels) | 15-40 minutes |
| free disk space | 3 GB (about 2 GB stay in `build` afterwards) | 3 GB |
| result | `fpdc.cdi` next to the script (about 380 MB) | `fpdc.cdi` next to the script |

Nothing is installed on your computer and nothing outside the project folder is written: the tools go to
`build\cache`, the intermediate files to `build\builder`, the result next to the script. Your game folder is only read. Delete
`build` afterwards to get the space back. A second run only redoes what changed (a few seconds when nothing
did).

**The disc image**: burn `fpdc.cdi` to a CD-R with a program that writes CDI images (DiscJuggler, or ImgBurn after
converting) and boot it on a Dreamcast that plays CD-Rs, or open it in an emulator (Flycast). It is a regular
self-booting MIL-CD image.

### What the first run downloads

Every file has a fixed version and is checked against its recorded SHA-256 before use (`tools/builder/downloads.json`).

| File | From | Size |
|---|---|---|
| Python 3.13.14 (Windows: embeddable; Linux: python-build-standalone) | python.org / GitHub (astral-sh) | 11 / 35 MB |
| Pillow 12.3.0, lz4 4.4.5 (Python packages) | pypi.org | 7 / 8 MB |
| ffmpeg 6.1.1 (Windows: gyan.dev essentials build) | GitHub (GyanD/codexffmpeg) | 88 MB |
| the Dreamcast program `fpdc.elf`, the CDI writer `mkdcdisc`, the ADPCM encoder `wav2adpcm`, the Linux ffmpeg | not downloaded: committed in `prebuilt/` (D-119) | - |

Windows: the downloads use the `curl.exe` and `tar.exe` that come with Windows 10 (1803) and 11 (older Windows 10:
PowerShell); Linux: `curl` or `wget`, `tar`, `sha256sum`.

To build without internet access, put these files (names as in `downloads.json`) next to the script, in a `prebuilt`
folder of the project, or in a folder named by the environment variable `FPDC_PREBUILT`: they are taken from there
(after the same SHA-256 check).

## Android data package

The Android port (`android/`) reads the same converted data as the Dreamcast build, but without a disc
image: the phone imports a zip of the data dir.

```sh
python3 tools/builder/fpbuild.py android "/path/Freedom Planet/Assets.dat"
# -> build/builder/fpdc-data.zip   (copy it to the phone: IMPORT DATA in the launcher)
```

That is the `data` target plus packing -- no mkdcdisc, no KallistiOS, no CDI. If you already built a CDI,
the data dir is `build/builder/data`; re-zip it with
`python3 tools/android/make_android_data.py build/builder/data -o fpdc-data.zip`. Building the APK itself
needs no game data at all (see **[android/README.md](android/README.md)**).

## Troubleshooting

| Message | What to do |
|---|---|
| `unknown Assets.dat (md5 ...)` | The file is not Freedom Planet 1.21.5 (GOG Windows `4085c983...` or Linux `9d00124a...`), or it was modified (mods). Use the file of an unmodified, up-to-date install. |
| `this Assets.dat ... is the Nintendo Switch build / a newer Linux build / an older release` | Only 1.21.5 is supported: update the game (GOG Galaxy) and drop its Assets.dat. |
| `Data/BGM ... not found next to the Assets.dat` | Drop the `Assets.dat` that is inside the game folder (it needs the `Data` folder beside it: music, voices, font), not a copy on its own. |
| `voice clip Data/voices/eng/... is missing` | The install is incomplete: verify / repair it in GOG Galaxy. |
| `WARNING: ... voice clips the port uses are 3.5 KB placeholders` | The Linux copy has placeholder clips for some lines; they stay silent. Build from the Windows `Assets.dat`, or add `--voices-pc "<Windows folder>\Data\voices"`. |
| `download failed` / `SHA-256 ... expected` | Check the internet connection and run again (finished downloads are kept). A firewall or proxy may block github.com, python.org or pypi.org: download the files listed above yourself and put them next to the script. |
| `ERROR: this builder needs 64-bit Windows` | 32-bit Windows is not supported. |
| `command failed ... last lines of ...` | A conversion step failed: the message names the step and its log (`build\builder\build.log`, one log per level in `build\builder\logs`). Please report it with that log. |
| Windows asks whether to run the file ("Windows protected your PC", or a security warning) | Files from a downloaded ZIP are marked as from the internet: choose *More info* > *Run anyway* (or *Run*). To avoid it, right-click the ZIP before unzipping > *Properties* > *Unblock*. |
| The window closes at once | Start `build_dreamcast.bat` from a command prompt to read the message: `build_dreamcast.bat "C:\GOG Games\Freedom Planet\Assets.dat"`. |
| Windows SmartScreen / antivirus | The script only runs Python, ffmpeg, mkdcdisc and wav2adpcm from `build\cache`. Antivirus scanning of the 30,000 small intermediate files can slow the build down a lot: excluding the project folder helps. |

Options (after the Assets.dat): `-j N` (levels built in parallel, default up to 4), `--force` (redo every step),
`--cdi FILE` (another output file), `--voices-pc DIR` (see above), `--local-tools` (developers, below).

## What the builder does

`build_dreamcast.bat` / `.sh` start a pinned portable Python (`tools/builder/bootstrap.cmd` / `bootstrap.sh`), which runs
`tools/builder/fpbuild.py`. Its steps, each skipped when its inputs, tools and outputs are unchanged (stamps in
`<data dir>/.fpbuild`; progress in `build/builder/build.log`):

1. identify the `Assets.dat` by its md5 and extract it with fp-assets (`third_party/fp-assets`). The Windows file is mapped
   to the image numbers of the Linux build that the pipeline uses (`tables/winmap_4085c983.json`, below);
2. the 57 acts (`tools/build_level.py`: import_frame, palettize, par_top);
3. the front end (`tools/import_ui.py` title / character select / stage select / time attack / gallery,
   `import_loading.py`, `make_vmu_icons.py`, `import_font.py`); 4. the ending (`tools/build_ending.py`); 5. gallery art and
   texts (`tools/import_gallery.py`);
6. music, 7. sounds and voices, 8. gallery sounds (`tools/audio/convert_audio.py`: ffmpeg + wav2adpcm);
9. the Adventure scenes and their story rooms (`tools/import_story.py`, from the committed `tables/story/`, docs/ADVENTURE.md);
10. one voice clip per story line (`convert_audio.py story`; a placeholder or missing clip leaves its line text-only);
11. a completeness check; then the disc: `tools/make_disc.py` (copy, pre-twiddle, pack, pad) and mkdcdisc with the
   Dreamcast program `fpdc.elf`. The disc dates are fixed (SOURCE_DATE_EPOCH), so the same input gives the same image.

Level layouts, animation tables, backdrops and the gallery / credits texts come from the original program, not from
`Assets.dat`; they are committed in `tables/` (`tables/README.md`), so the Windows `Assets.dat` alone is enough.

### Windows Assets.dat

fp-assets reads the GOG Windows file (17,162 images, 475 sounds). Compared with the Linux file: the 475 sounds are
byte-identical; 16,866 of the Linux build's 16,885 images are byte-identical under ids shifted by 0-22 (29 runs in the
table). The other 19 exist only in the console / Linux build: 11 of them (Milla's HUD panel with the pad labels B / C,
the dig hint "down + B") are rebuilt from the PC images (keyboard labels ATK / SPC) by the table's label recipes, which give
the Linux pixels exactly; the rest are not drawn by the port (the original title's PRESS (X), the Mahjong hints, the Wii U
sign) or almost identical (the fade edge: black 0,0,0 instead of 8,0,0) and are taken from the PC version or left
transparent. Result: 16,879 of 16,885 images are identical to the Linux extraction (pixels and hot spots).

## Developers

**Local tools instead of the downloads**: `./build_dreamcast.sh <Assets.dat> --local-tools` (or `FPB_LOCAL=1`; on Windows
`set FPB_LOCAL=1` before the .bat, with `python.exe` on the PATH) uses this computer's `python3` (with Pillow and lz4),
`ffmpeg`, `cc` (wav2adpcm from `third_party/wav2adpcm`), mkdcdisc (`$MKDCDISC`,
`third_party/mkdcdisc/builddir/mkdcdisc` or the PATH) and, when KallistiOS is set up (`$KOS_BASE/environ.sh` or
`/opt/toolchains/dc/kos/environ.sh`), compiles `fpdc.elf` from this checkout. Whatever is missing is still downloaded.
`--elf FILE` uses another Dreamcast program.

**Only the data dir**: `python3 tools/builder/fpbuild.py data <Assets.dat> [--out DIR] [--work DIR] [-j N] [--force]`.

**The manual pipeline** (ASSET_PIPELINE.md) still works as documented: `tools/build_level.sh`, `tools/import_ending.sh`,
`tools/audio/convert_bgm.sh` / `convert_sfx.sh` / `convert_gallery.sh` call the Python versions, `make cdi` /
`make cdi-image` call `tools/make_disc.py`. **When a session adds a level or a sound, edit the Python files**: `LEVELS` in
`tools/builder/fpbuild.py`, the `SFX` list in `tools/audio/convert_audio.py`, a UI command in fpbuild's `s_ui`.

**Checks**: `python3 tools/builder/fpbuild.py selftest` (pins, tables, local copies of the downloads);
`diff -rq -x .fpbuild build/builder/data converted2` compares a build with the lead's tree.

**Tables**: `tools/builder/make_tables.py layout <bin64/Chowdren> build/re tables` (after a change to the dump tools or
a new List* constructor), `make_tables.py winmap` / `labels` for the Windows map (tables/README.md).

**Prebuilt files** (`prebuilt/`, D-119): `tools/builder/build_release.sh [--program-only]` builds `fpdc.elf`
(KallistiOS) and, without `--program-only`, wav2adpcm, mkdcdisc and the Linux ffmpeg for Windows and Linux from pinned
sources into `prebuilt/`, writes `prebuilt/SHA256SUMS` and updates their entries in `tools/builder/downloads.json`
(fpdc.elf: also `src_tree`). Commit `prebuilt/` and `downloads.json` together. **`prebuilt/fpdc.elf` must match the
checkout**: after a change of `src/`, rebuild it in the same change; `fpbuild.py selftest` fails while it is older than
`src/`.
