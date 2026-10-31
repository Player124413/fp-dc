# A Freedom Planet Decompilation targetting Dreamcast.
Fan project, not affiliated with GalaxyTrail. Freedom Planet assets belong to their owners.


Through a mixture of decompilation and guesswork - followed by compression and some other optimization bulldada - I present what is effectively a FOSS approximation of Freedom Planet. This project was entirely built with Claude's Opus 5.5, with myself merely play-testing and prompting the majority. Huge credits to the https://github.com/snickerbockers/fp-assets tools for making this possible, as well as making it known to myself that the debug symbols for this game were actually out there.

## Compression, optimization?
Graphics were shrunk by converting every texture to 8-bit (and small overlays to 4-bit) indexed colour, sharing palettes across each level, packing sprites into compact texture sheets, and storing them on disc pre-formatted for the graphics chip and LZ4-compressed. Audio was compressed to 4-bit ADPCM with streamed music, and sound effects were split into shared, per-character and per-act sets, the act sets at half rate with silent tails trimmed, so only what the current act needs sits in the 2 MB of sound memory. Anything rarely used, like shield sprites, gallery artwork and results voices, is loaded only when it’s actually needed. Unfortunately, this is all baked into the codebase, and thus not removable in PC builds. In practice, this means that it runs at ~42MB of RAM on PC (up to 6x lower than retail) with little visual difference. It also means that it runs at a solid 60fps in all stages on a real-hardware Dreamcast, excluding screens wherein multiple large interactive moving sprites are (notably the Dragon Valley final boss and the chase cutscene in Relic Maze).

## How much is decompiled?
Roughly 90% decompiled, 10% inferred. The repo has about 45.7k lines of C. About 42k of those are gameplay (game/, characters/, physics/), and in that code about 7,000 comment lines cite the original’s events or collision handlers. Those are recovered from the decompile (RE) or checked against recordings of the original game (RV). Only about 550 lines are marked INFERRED, and about half of those still cite an original event: they fill a gap inside logic that was otherwise recovered. Another 18 are marked UNKNOWN. This is a rough heuristic based on how the code is labelled, not a line-by-line audit.

## Current issues?
See https://github.com/Wangblows2000/fp-dc/blob/master/TODO.md

## Building for Dreamcast

> **Easy builder:** no tools to install. Drag your Freedom Planet **1.21.5** `Assets.dat` (the GOG Windows one or the
> Linux one) onto **`build_dreamcast.bat`** (Windows) or run **`./build_dreamcast.sh /path/to/Assets.dat`** (Linux).
> It builds everything by itself and writes the disc image **`fpdc.cdi`** next to the script. No compiler is needed: the
> program itself is ready-made in `prebuilt/`. (Unzipped inside your Freedom Planet folder, a double-click is enough: it
> finds the `Assets.dat` by itself.)

1. Download this repository (*Code* > *Download ZIP*) and unzip it.
2. Drag `Assets.dat` from your Freedom Planet folder onto `build_dreamcast.bat` (Windows), or run
   `./build_dreamcast.sh "/path/to/Freedom Planet/Assets.dat"` (Linux).
3. The first run downloads Python and ffmpeg (about 110 MB on Windows, 55 MB on Linux, checksum-verified, kept in
   `build/cache`) and takes 20-60 minutes; later runs only redo what changed. About 3 GB of free disk space is needed.
4. Burn `fpdc.cdi` (in the same folder as the script) to a CD-R (DiscJuggler, or ImgBurn after converting) for a Dreamcast that boots CD-Rs, or open
   it in an emulator such as Flycast.

Your game folder is only read. No game data is stored in this repository: the builder takes the art, sound and music
from your copy. The only game-derived files here are the level layout / animation tables and the story tables (scene list,
dialogue text) in `tables/` (see its README).
Details, troubleshooting and the developer build (KallistiOS toolchain, `make && make cdi`) are in **[BUILDING.md](BUILDING.md)**.

## PC port (Linux and Windows)

A PC version of the same code (SDL2; Linux x86_64 and 32-bit Windows) lives on the
**[`pc-port` branch](https://github.com/Wangblows2000/fp-dc/tree/pc-port)**. It has the same drag-and-drop builders:
drop `Assets.dat` onto `build_win32.bat` to get a working `fpdc.exe`, or onto `build_linux.sh` for a Linux executable.

## Android port

A native Android port of the same code (OpenGL ES 3, AAudio; no Java game code and no emulator) lives in
**[`android/`](android/README.md)**. It is a platform and renderer layer only: the game in `src/game` is the
same code the Dreamcast build runs, drawn through `src/renderer/render_gles.c` at the native 426x240 and
upscaled by the GPU.

- **Launcher**: PLAY / CONTROLS / SETTINGS / ABOUT / DATA screens drawn by the port's own immediate-mode UI
  (`src/platform/android/`), with the data import, the port settings and the controls editor in them.
- **Touch controls**: a floating analogue stick, JUMP / ATTACK / SPECIAL / START / GUARD / GEAR buttons, four
  presets, and an in-game editor where every control is dragged, resized and faded -- the layout is stored in
  `port.txt` and the editor opens from the GEAR button or the BACK key while playing. Gamepads work at the same
  time (the pad and a controller are ORed together).
- **Data**: the APK ships no game data. Convert your `Assets.dat` with the normal builder, pack `converted/` with
  `python3 tools/android/make_android_data.py converted -o fpdc-data.zip`, copy the zip to the phone and use
  **IMPORT DATA** in the launcher.

Prebuilt APK: the CI builds the port with the NDK on every push; a successful run leaves `app-release.apk` in
the `fpdc-apk` artifact of the *Build APK* job ([`.github/workflows/android.yml`](.github/workflows/android.yml)).
Every build carries the same signing key and a growing `versionCode`, so a new APK installs *over* the old one
(`adb install -r`, or just tap the file) — no uninstall, and the save data and imported game data stay.

Something wrong on the phone? The port writes `fpdc.log` next to its data
(`/sdcard/Android/data/com.fpdc.port/files/`) and **ABOUT -> SHARE LOG FILE** in the launcher sends it
anywhere. The Dreamcast side of the same data conversion is a workflow too:
**[Convert game data](.github/workflows/convert.yml)** takes a download link to your archive and gives back
`fpdc.cdi` (Actions tab -> Run workflow).

Host-side checks (no NDK, no device): `make -C tools/android check` -- syntax-checks the GLES backends against
stub NDK headers, verifies the Dreamcast texture twiddle against `tools/twiddle_disc.py`, and renders every
launcher screen to PNG with the shared UI code. Details in **[android/README.md](android/README.md)**.
