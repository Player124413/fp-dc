# Freedom Planet — Android port

A phone/tablet port of the same code base as the Dreamcast build. There is no emulator and no Java in the
game: `src/game` runs as-is, the picture goes through an OpenGL ES 3 renderer at the native 426×240 and is
upscaled on the GPU, and the port adds a launcher, editable touch controls and an audio mixer around it.

```
src/platform/android/plat_android.c   lifecycle, EGL, input (touch + gamepad), storage, the 60 Hz frame loop
src/platform/android/snd_android.c    AAudio mixer: AICA ADPCM decoding, 24 voices, streaming music
src/renderer/render_gles.c            r_* backend: paletted textures, ETC2, FBO upscale, blend modes
src/renderer/ui_gles.c                ui.h backend: the shell's immediate-mode UI (flat + SDF batches)
src/platform/android/{shell,launcher,ctl_editor,pad_draw,widgets,touch,cfg}.c   the shell
android/                              Gradle project, manifest, FpActivity.java, CMakeLists.txt
tools/android/                        host-side checks, the data packer, the font generator, NDK stubs
```

## What it looks like

The images below are rendered by the port's own UI code through the host backend
(`tools/preview/ui_soft.c`) — the same `shell.c` / `launcher.c` / `ctl_editor.c` / `pad_draw.c` that run on
the phone, so they show the real layouts:

| launcher | settings | controls editor |
|---|---|---|
| ![launcher](../docs/android/launcher.png) | ![settings](../docs/android/settings.png) | ![editor](../docs/android/editor.png) |

| touch pad in game | in-game |
|---|---|
| ![pad](../docs/android/touchpad.png) | ![ingame](../docs/android/ingame.png) |

Regenerate them with `make -C tools/android preview`.

## Build

Requirements: Android Studio (or the command-line SDK + NDK 26/27 + CMake 3.22 + JDK 17).

```sh
cd android
gradle wrapper --gradle-version 8.7     # once (or just open the folder in Android Studio)
./gradlew assembleDebug                 # -> app/build/outputs/apk/debug/app-debug.apk
./gradlew installDebug                  # or run it straight onto a connected phone
```

`local.properties` needs `sdk.dir=...` (Android Studio writes it). ABIs built: `arm64-v8a`, `armeabi-v7a`,
`x86_64`. `minSdk 26` — AAudio, the low-latency output, starts there; GLES 3.0 is required by the
manifest and is universal since 2013.

### 32-bit devices

One APK runs on both: `arm64-v8a` is the library a 64-bit phone loads, `armeabi-v7a` the 32-bit one, and
Android picks. Nothing has to be selected at install time, and an update from a 64-bit build to a 32-bit
one (or the other way) is still just an install over the top.

* **The build cannot lose it.** `assembleDebug` builds every ABI in `abiFilters`, and the CI's
  *Check what went into the APK* step fails the run when `lib/armeabi-v7a/libfpdc.so` is missing or is
  not a 32-bit ELF (`readelf -h`), printing the size of each ABI it found. `make -C tools/android check`
  runs `check_abi.py`, which refuses a `build.gradle` whose `abiFilters` no longer list both.
* **The log says which one ran.** The header has a `build: armeabi-v7a, 32-bit` line (or `arm64-v8a,
  64-bit`), and the ABOUT screen's BUILD row reads `V1.1  ANDROID  ARMEABI-V7A  32-BIT`. A crash report
  from a 32-bit phone is a different `.so` (`unzip -p fpdc.apk lib/armeabi-v7a/libfpdc.so`), a 32-bit
  address space and usually a weaker GPU: the line saves guessing.
* **What a 32-bit phone is limited by** is memory and the GPU, not the code: the physics is fixed point,
  audio positions are `int64_t` (a 16.16 position used to overflow an `int` on long tracks), files are
  loaded one at a time and capped at 64 MB each, and the map/texture paths use `size_t`, so nothing
  truncates on a 32-bit build.
* **A device without OpenGL ES 3.0 has to say so.** The old 32-bit tablets are exactly the ones with an
  ES 2.0-only driver (Mali-400 and friends). The port cannot draw a pixel there, and before this the
  window stayed black with the reason inside a log that a black screen cannot reach. Now the EGL
  failure prints the ES 2.0 version and renderer into the log (`gfx: the device offers OpenGL ES 2.0 ...`)
  and shows a Toast: *"this device has no OpenGL ES 3.0 ... Log: /data/user/0/com.fpdc.port/files/fpdc.log"*.

### Installing an update: no uninstall

Every build is signed with the same key, `android/keystore/fpdc.p12` (a sideload key, committed on purpose:
Android refuses an update whose signature differs, and that is what forces an uninstall). `versionCode` grows
with the CI run number, so a new APK is always an *update*:

```sh
adb install -r app-debug.apk            # signed with the committed key: installs over the previous build
```

The CI checks this on every build: it verifies the APK with `apksigner` and fails when it is not signed.
Only a debug APK is built (there is no `assembleRelease` step any more) -- but it is signed with the same
committed key, so it replaces the previous build just like a release one would.

## Game data

The APK contains no Freedom Planet data. The game reads its assets from
`<external files dir>/data` (`/sdcard/Android/data/com.fpdc.port/files/data`):

1. One command on a PC converts your own `Assets.dat` (1.21.5) and packs it:
   `python3 tools/builder/fpbuild.py android "/path/Freedom Planet/Assets.dat"` →
   `build/builder/fpdc-data.zip` (20-60 min the first time, ~3 GB of disk; it downloads ffmpeg itself).
   Already have a converted data dir (from a CDI build)? `python3 tools/android/make_android_data.py
   build/builder/data -o fpdc-data.zip`.
2. Copy the zip anywhere on the phone, start the port, **IMPORT DATA**, pick the zip.

The importer unpacks into `data.tmp` and swaps the folder in at the end, so an interrupted import cannot
corrupt a working install; progress is shown in the launcher. A `data` folder copied by other means (file
manager, `adb push`) works too — the only thing the importer adds is the `.fpdc-data` marker with the size.
The port reads the same files the disc has (PAL8/PAL4/ARGB1555 atlases, AICA ADPCM sound effects, ADPCM
music), so the package is the Dreamcast `converted/` tree, unpacked.

## Controls

- **Touch pad**: floating analogue stick (it appears where the thumb lands), JUMP / ATTACK / SPECIAL /
  START / GUARD / GEAR. Sliding from one button to another re-targets the press; short taps are latched so a
  fast tap is never lost between frames. One vibration per press event, not per frame.
- **Editor**: the GEAR button in game, or CONTROLS in the launcher. Select a control, drag it (2.5 % grid),
  and the toolbar next to it sizes it / fades it. Presets (DEFAULT / LEFTY / COMPACT / BIG), TEST PAD, RESET.
  Everything is stored in `port.txt`, along with the per-control positions.
- **Gamepad**: any controller Android recognises (buttons, dpad, sticks, triggers) is ORed with the pad.

## Performance

- The view is rendered into a 426×240 texture and upscaled once per frame: fill cost drops by ~20× at 1080p
  and the pixel grid stays uniform (`render.mode = PIXEL`; `SHARP` draws straight at display scale).
- The upscale places the view with `present_geom.h` (one function for the rectangle and for the texture
  coordinates of that rectangle) — `tools/android/check_present.c` pins it. With `video.letterbox` the view
  keeps its exact 426×240 shape and the screen around it is filled with the same frame smeared and dimmed
  (no black bars, nothing stretched, nothing cut off); with it off the picture is stretched to the screen.
  The game's own SCREEN SIZE option (`r_set_letterbox`, a Dreamcast 4:3 thing) does not move the phone's
  presentation: use SETTINGS → ASPECT.
- Paletted atlases are uploaded as the index byte itself (`GL_R8`) with the level palette as a 256×4 lookup
  texture sampled with `texelFetch`: 8 bpp in texture memory, the same as the Dreamcast, no CPU conversion,
  and `r_pal_load` costs one 1 KB upload. ETC2 atlases (FPTX fmt 5/6) are also supported and uploaded as
  compressed blocks; they are optional, see below.
- One interleaved vertex buffer, quads batched per (texture, blend) with 16-bit indices: a game frame is a
  handful of draw calls. Draw order is the engine's submission order.
- The frame loop is a fixed 60 Hz accumulator: on a 90/120 Hz panel the game runs 60 update+draw pairs per
  second instead of double-stepping, and the 30 fps mode draws every other frame.
- Sound effects are decoded once at load (0.1–3 s each); music is decoded by a worker thread into a 1 s ring
  so the audio callback never touches the filesystem. AICA ADPCM is decoded in software with the hardware's
  step function (`aica_adpcm.h`): one byte is one stereo frame (high nibble left), two nibbles are two mono
  samples (low nibble first, as `wav2adpcm` packs them).
- One AAudio stream for the whole app: `snd_init_state()` returns the running stream instead of opening a
  second one (a second consumer of the same rings runs the music at double tempo and starves it). The
  stream is rebuilt if the device takes it away, and the delivery rate is measured once after four seconds:
  if a phone does not run at the rate it reports, the tempo follows the measured rate.
- A sound handle is `<generation> << 16 | <slot>`: the port's id table and its voice tables keep handles across a
  session, `engine_stop()` frees every sample, and the next session's `snd_load()` gives those slots to other files.
  A bare slot number then played whatever moved in, which is how a boss line ended up where Lilac's voice belongs
  (reports of "a man's voice in the character select" and "when she flies"). A handle of a freed sample now matches
  no sample and plays nothing; `game_snd_forget()` drops every table when the samples go, and
  `tools/android/check_audio.c` pins both. The log names the file an id plays, once per id:
  `snd: id 220 -> sfx/s220.wav` (or `snd: id 220 plays sfx/s99.wav, expected sfx/s220.wav` when a table is stale).
- The mix is bent, not clipped: the music alone sits near full scale, so a voice or an effect on top of it used to be
  torn off flat (heard as a broken soundtrack). Below 24000 the sum is untouched, above it a smooth knee takes the
  peaks; and music is resampled from 32 kHz with a four-point curve instead of a straight line between two frames,
  which had been folding everything above 16 kHz back into the mix.
- The tempo chain is the stream rate, the source rate (music 32 kHz, effects 22.05 kHz) and the step per
  output frame; `tools/android/check_audio.c` pins it (a 0.1 s mix must consume 0.1 s of source), together
  with the stream lifetime and the ADPCM layouts.

## Settings (`port.txt`, in the app's internal files dir)

| key | default | |
|---|---|---|
| `render.mode` | 0 | 0 = 426×240 texture upscale, 1 = draw at display scale |
| `render.filter` | 0 | 0 = nearest (the pixel grid), 1 = smooth when upscaling |
| `video.letterbox` | 1 | keep the view's shape (0 = stretch to the screen) |
| `render.scanlines` | 0 | subtle 3-row scanlines |
| `render.fps` | 60 | 30 halves the draw rate |
| `sys.showfps` | 0 | FPS counter |
| `sys.keepon` | 1 | keep the screen on |
| `sys.lang` | 0 | 0 = English, 1 = Russian (the SETTINGS > SYSTEM row writes it) |
| `sys.credits` | | the build whose thanks window was already shown |
| `audio.music` / `audio.sfx` | 80 / 90 | master volumes (the game's own options still apply) |
| `pad.*` | | the touch layout written by the editor |

## Restarting the engine

EXIT TO LAUNCHER runs `engine_stop()` -> `game_unload_all()` -> `r_reset()`, which empties the whole texture
table, and the next PLAY runs `game_init()` again in the same process. Anything that caches "I loaded this
already" must therefore be able to tell that its textures are gone: the front end asks `r_tex_alive()` before
trusting its own flag, and each `tex_t` carries the load generation of its slot, so a handle from before the
reset can never draw (or free) the atlas that took the slot afterwards.

## Leaving the game

The launcher's home screen is PLAY (or IMPORT GAME DATA), CONTROLS, SETTINGS, ABOUT, **COPY LOG**, **CHECK FOR
UPDATES** and **CREDITS**: the log of the session that just went wrong is one tap away, without going into the
game or plugging in a cable.

Two windows belong to no screen. They are drawn *instead* of the screen under them (`launcher_draw` returns
early), so a tap can only reach their own buttons, and BACK closes the window rather than the app
(`shell_popup_close()` from `do_back()`):

- **CREDITS** -- opens by itself once per build (`sys.credits` remembers which build was already thanked)
  and any time from HOME or ABOUT. It says that the port stands on the original Dreamcast port and turns
  both addresses into buttons: `github.com/Wangblows2000/fp-dc` and the author's channel
  `t.me/player1444ports`;
- **CHECK FOR UPDATES** -- the answer window: *asking / this is the newest build / there is a newer one
  (number) / the check failed*, plus a button that opens the builds page in the browser.

## Language, links and the update check

The launcher speaks English and Russian. `i18n.h` is the whole mechanism: `tr(en, ru)` returns one of the
two strings, `fp_ru` is the switch, and a string that was never translated is simply the English one. The
setting is `sys.lang`, written by SETTINGS > SYSTEM > LANGUAGE (and the row itself flips to Russian on the
same tap).

The shell font is a 5×7 bitmap (`ui_font.h`, generated by `tools/android/make_font.py`) and it only covered
ASCII, so `ui_fontx.h` adds the Cyrillic capitals U+0410..U+042F and Ё by hand, plus the UTF-8 reader that
both rasterizers use (`src/renderer/ui_gles.c` on the phone, `tools/preview/ui_soft.c` in the preview). Width
is counted in characters, not bytes -- otherwise every Russian label would be measured twice its size.

Neither a link nor an HTTPS request can be done from C (the NDK ships no curl), so both go through the
activity in `FpActivity.java`:

| shell call | Java | |
| --- | --- | --- |
| `shell_open_url(url)` | `openUrl()` | `ACTION_VIEW` intent: browser, Telegram, whatever owns the scheme |
| `shell_check_updates()` | `checkUpdates()` | worker thread, `HttpURLConnection` + `org.json` on `api.github.com/repos/Player124413/fp-dc/actions/workflows/android.yml/runs?status=success&per_page=1`, then `nativeUpdateResult(code, build, url, msg)` back into C |

The comparison is exact rather than a guess from a version string: the APK's `versionCode` *is* the GitHub
run number (`build.gradle`), CMake passes it into the library as `FP_VERSION_CODE`, and the newest successful
run_number from the API is compared with it. A host build has no number (`FP_VERSION_CODE` = 0), so it reports
"newest" and never auto-opens anything.

In game, the hardware BACK button or the pad's gear opens the pause menu: RESUME, CONTROLS, SHARE LOG,
COPY LOG, EXIT TO LAUNCHER. The menu freezes the game (`shell_game_paused()` also covers the controls
editor) and EXIT stops the engine on the next tick -- `shell_request_exit()` from the menu,
`shell_take_exit()` by the platform loop, then `engine_stop()` -- which drops back into the launcher with
the data and the save intact.

## The log (what to send when something is wrong)

A story room's script comes from RV traces of the original, where a room recycles the memory of the objects it
destroys, so a heroine's recorded "visible" flag can be wrong while she stands in the view (the Dragon Valley opening
had Carol speaking over an empty jungle). The room therefore draws a heroine on the strength of her image and her spot
in the view, and says so once per actor: `story dv_meet: actor 10 (skin 449) drawn with v=0 at 371,120`. A row the
room's `_ens` bank is short of is replaced by the nearest pose of the same object, and the log names it
(`story <id>: row s449_13_0 is not in the room's bank, using s449_13_16`).

Every line the port logs goes to `fpdc.log` twice: logcat (tag `fpdc`, `fpdc/ui`) and a file

```
/sdcard/Android/data/com.fpdc.port/files/fpdc.log          <- the current session
/sdcard/Android/data/com.fpdc.port/files/fpdc.prev.log     <- the session before it (a crash survives here)
```

Three ways to get it out: **COPY LOG** (clipboard, paste it into the chat), **SHARE LOG** (any messenger or
mail app) and **SAVE LOG AS .TXT** (Downloads), all on ABOUT and in the in-game pause menu. What they send is
the *previous* session's tail followed by this one, because after a crash the session that died is the one in
`fpdc.prev.log` -- a native crash (signal + `_Unwind_Backtrace` frames) and an uncaught Java exception both
append themselves to it. `sys.sndtrace` (SETTINGS -> SYSTEM -> SOUND TRACE) logs every voice start, which is
how a report about "extra sounds" says which sound it is.

The file starts with the build (the ABI and whether it is the 32-bit or the 64-bit library), the device, the
Android API level, the window in pixels and dp, and the data folder, then
one line per event with a relative timestamp, and every 150 frames a summary of what the renderer and the mixer
actually did:

```
build: arm64-v8a, 64-bit
device: HONOR ALI-NX1, android 35, 2652x1200 px at 3.25x (816x369 dp)
...
frame 300: win 2652x1200 dp 3.25 ui 2652x1200, 812 quads + 63 sdf in 2 calls, glerr 0x0,
           game (mode 1 lvl dv1 p 1234,300 f 510, game 152 quads, 71 tex slots, 0 sprites skipped),
           rss 214 MB, snd 6 voices/0 underruns/0 steals m_stage1.adp
```

Two numbers there are for "the sprites are missing": **tex slots** is how many texture-table slots are in use
(the ceiling is 512; a level needs about 70, and a number that keeps climbing for minutes means something is
loading textures it never frees), and **sprites skipped** counts sprites the renderer refused to draw because
their texture handle was dead -- a steady count there means a bank failed to load. A failed load is always
logged now (`tex ...: missing`, `tex ...: invalid`, `gles: texture not FPTX`, `gles: texture WxH is not power
of two`, `bank ...: frame N (...) outside its ...x... atlas`, `bank ...: invalid file`), and so is a palette
group that came out fully transparent (`pal 2: every entry transparent, forced opaque`) -- paletted sheets
draw nothing at all in that case.

The `game (...)` part is the engine's own state: the mode (`0` frontend, `1` a stage, `2` results, `4` ending,
`5` a story room), the level directory, the player position and the frame counter, then **how many quads the
game pushed into the frame** -- `0` means the picture is a bare clear, which is never a real frame (even a fade
to black draws its dimmer). Read it first when the screen is wrong: `0 quads` is a drawing problem, a `p` of
`nan` is a physics problem, and a plausible picture with 0 quads cannot happen.

(voices that keep counting up mean a sound that never ends -- that is what "extra sounds" looks like; underruns
counting up mean the audio callback is being starved; **steals** counting up means the 64 voices are all busy, so
effects are being cut off by newer ones -- "the sound effects disappeared" is what that sounds like; rss climbing
means a leak or a texture that never goes away.)

If the engine draws nothing for a second, the port says so on screen instead of leaving a silent black screen:

```
NO PICTURE - REPORT IT (COPY LOG)
```

That banner is not a crash: the app is running, the touch pad works, and the engine produced no geometry. The
log then has a line `!! 60 frames with no geometry: mode ..., lvl ..., p ..., f ...` with the same state.

The heroine is traced too. Every quarter of a second of a new level, and on every draw whose frame rect
does not fit its atlas, the log gets a line like

```
player skin: anim "run" frame 2, rect (128,64 41x47) in 512x256, flip 1, rot 0.0
player hair: anim "h_run" frame 1, rect (0,32 38x22) in 256x128, flip 1, rot 0.0
```

which is what a report about "the sprite looks wrong" is answered with: the animation and frame she is
actually in, the rect inside the atlas, and the tilt/flip the draw ran under. A tilt far from 0 here while
she stands on flat ground is a terrain-angle bug; a rect wider than the atlas is a frame bug. The draw
also refuses to inherit a squash-and-stretch from a gimmick: it forces the neutral scale first and says so
in the log (`player: drawn under a leaked scale 2.00 x 1.00, reset`).

The rect alone cannot tell "the sheet is empty under it" from "the palette hides it", so the first four
draws of a level also read the atlas out into the log (the Android backend keeps the texel indices of every
paletted atlas for exactly this; it is freed with the slot):

```
sprite report: frame (128,64 41x47) of its 512x256 atlas, palette row 0 (61 of 256 opaque)
sprite report: 1927 texels: 1650 draw, 0 have an index but are invisible in that palette, 277 empty
sprite report: |..........##################.......|
sprite report: |....#############################.....|
...
```

`#` is a texel that draws, `-` has an index but its palette entry's alpha bit is 0 (invisible however right
the geometry is) and `.` is index 0, nothing in the atlas at all. Rows and columns in which nothing draws
are named as ranges before the picture. This is what a "she is not drawn all the way" report needs: a hole
in the middle of the `#` area is the sheet, a `-` band there is the palette, and an all-`#` picture means
the frame is fine and something else is covering her.

### What the picture got (the sprite dump)

The atlas says what the sheet holds; it cannot say what reached the screen, and "she is cut off" is the
same sentence for three different bugs. So the first eight **distinct poses** of a level are read back off
the target three times around her quads -- before them, right after them, and at the end of the frame -- and
the three masks are written next to the atlas', one character per texel of the same rect:

```
sprite dump: anim "idle" frame 0, rect (611,65 35x48) of 1024x256, at (12,168), hotspot (17,47), rot 0.0, flip 0, box (0,121 36x49)
sprite dump: atlas  |......####....#######..##..........|
sprite dump: drawn  |......####....#######..##..........|
sprite dump: over   |...................................|
sprite dump: atlas  1680 texels marked '#', 0 unchanged, 0 outside the picture
sprite dump: drawn  1680 texels marked '#', 0 unchanged, 0 outside the picture
sprite dump: over   0 texels marked '+', 1680 unchanged, 0 outside the picture
```

* `atlas` -- what the sheet holds (`#` draws, `-` hidden by the palette, `.` empty), the same mask as
  `sprite report` above, printed for the exact draw the other two belong to.
* `drawn` -- `#` where the picture **changed** when her quads were drawn: her own coverage, as the GPU saw
  it. A texel the atlas marks and `drawn` does not is a pixel her quad never wrote.
* `over` -- `+` where the picture changed again *after* her quads: something was drawn on top of her. Every
  sprite or rect that lands on her box in that window is named right before it
  (`sprite dump: sprite after her: atlas 3 (512x256) rect (0,64 64x32) at (240,96) 64x32 additive 0 invert 0`).

Reading the three together answers the report without guessing: an all-`#` `drawn` with an all-`.` `over` is
a correct frame, a `drawn` that follows the atlas but an `over` with `+` on a straight edge is an overdraw,
and a `drawn` that is missing a slab the atlas has is the quad itself. Only the first eight poses of a level
are dumped (three read-backs each, ~150 lines); the rest of the level logs nothing. **Story scenes are
covered too**: `csx_draw_actors()` runs the same three read-backs around every character it draws (props
are skipped), so a "her sprite is broken in the cutscene" report is answered the same way as a gameplay
one -- and the `over` pass names whatever is painted on top of her, which is how a stray prop or a dust
effect drawn over the heroine gets identified instead of guessed at.

A story scene also writes its **cast** into the log, because "Carol's bike is missing" or "the cutscene
plays wrong" needs the room's actor table, not a guess. For the first three scenes of a run, one line per
actor, then the first draws of every scene with the spot each one lands on:

```
story dv_meet: actor 2 is playerskincarol_449 on layer 2 (skin 1)
story dv_meet: actor 3 is carolbike_450 on layer 2 (skin 0)
story dv_meet: actor 4 is playerskinlilac_447 on layer 2 (skin 1)
story dv_meet: actor 3 is carolbike_450 on layer 2 (skin 0)
story dv_meet: playerskincarol_449 (actor 2) row 812 at (312,120) vis 0 alpha 255 flip 0
```

`is <class>` is the traced class name (a heroine, a prop, an effect), `layer` is the Fusion layer it sits on,
and the draw line says which row of the `_ens` bank it uses, **where on screen it lands** and whether the
script's own visibility flag was 0 (the flags of a room's recycled objects are not trustworthy, see
`src/game/story_draw.h`). Two actors printing the same `at (...)` are drawn on top of each other, and a
class that never prints a draw line is **not drawn**: above, the bike is in the cast but has no draw line --
a prop keeps its own visibility flag (D-126: drawing props on their placement alone put an airship over the
Character Select screen), so a staged prop with a spoiled flag has to be fixed in the scene data, where the
log points at the class and the row it needs.

A frame whose rect runs past its atlas is **drawn whole**, sampling past 1.0 with `GL_CLAMP_TO_EDGE`: that
is what the Dreamcast backend has always done (it has no guard at all, the PVR clamps the sample to the edge
of the texture), and cutting the quad here instead used to lose a strip of the picture that was whole on the
Dreamcast. The overrun is still written into the log (`gles: frame (...) clipped to its ... atlas`).

## Audio: pause, focus and the stream

Two separate things stop the mixer, and both are in the log:

```
snd: mixing stopped (reasons 0x4)
snd: mixing still stopped (reasons 0x1)
snd: mixing restored (reasons 0x0)
```

The reasons are a mask -- `0x1` the app paused, `0x2` the window went away, `0x4` the focus was lost -- and
the stream runs again only when every one of them has been cleared. They do not arrive in pairs on every
phone (a terminated window has no `INIT_WINDOW` resume on some launchers), so one flag used to be able to
leave the stream paused for good while the game kept running: "the sound is gone after a pause". The frame
log line carries the same mask (`snd 6 voices/0 underruns/0 steals (susp 0x0) m_stage1.adp`).

The **pause panel** of a story scene holds the samples instead of killing them (the original's
`Media::pause_samples()`): the scene's ambient loops and the line's voice come back where they stopped, and
the panel's own confirm beep is heard. They used to be stopped, which sounded like "half of the audio goes
away when you pause".

The **music stream** is a one-second ring the worker thread fills and the AAudio callback drains, and the
log says what that ring did:

```
snd: the music ring ran dry 312 times (track bgm/m_stage1.adp, 48000 Hz out)
snd: a track change took the ring 3 times (track bgm/m_stage2.adp)
snd: 47995 frames/s delivered, the stream reports 48000 Hz
```

`ran dry` is the moment the callback wanted a sample the worker had not written yet: the music stops for as
long as it takes to refill, which is heard as a hitch. `a track change took the ring` is normal on every
switch (a request zeroes the ring and starts the next track there); what is not normal is a *large* count,
which means something keeps restarting the track. The rate line is measured over four seconds and adopted
only when two windows agree -- one window read while a stage loads is low, and resampling to that number
made the music drift and hitch for the whole session (D-127).

Two things the Android path has to do the way the Dreamcast does (D-128). A `music_play()` for the track
that is *already playing* is ignored -- the DC's `music_play()` never restarted what it had, and the game asks
for its track more than once from several states; here that cut the music back to its first sample each time.
And the output stream carries a cushion: a 512-frame callback with a 2048-frame buffer (~43 ms), plus a
two-second music ring, because the DC played two seconds out of the AICA's own buffer and a hardware sound
chip cannot be late. A missed callback deadline is a dropped block -- a hitch no mixer code can avoid.

A native crash writes a self-contained block into the log before the system takes the process:

```
---- native crash: signal 11 at 0x77f6e45010, thread 12345 ----
     fault: outside the loaded modules (no module: heap, stack or unmapped)
     state: game, screen 2, pause 0, rss 214 MB, snd 6 voices / 0 underruns / 0 steals, music bgm/m_stage1.adp
     game: mode 1 lvl dv1 p 1234,300 f 510, 152 quads
  #00 0x752dfd2ab4  libc.so+0x9cab4
  #01 0x78dc3a7004  libfpdc.so+0x1a7004
  ...
     modules:
       libfpdc.so                   0x78c0000000-0x78c01b4000
       ...
```

The `<module>+<offset>` part is the point: the loader places every `.so` at a random address, so a bare address
means nothing. The APK ships `lib/arm64-v8a/libfpdc.so` unstripped for exactly this, so an offset resolves
offline against the same build:

```sh
unzip -p fpdc.apk lib/arm64-v8a/libfpdc.so > libfpdc.so
addr2line -f -C -e libfpdc.so 0x1a7004
```

An uncaught Java exception is appended as a stack trace, and a native crash leaves its last lines in
`fpdc.prev.log` after the restart.

**ABOUT -> SHARE LOG FILE** hands the file to any messenger or mail app (it is sent as text, so no file
permissions are involved). The same file can be pulled over USB/adb:

```sh
adb pull /sdcard/Android/data/com.fpdc.port/files/fpdc.log
```

## Prebuilt APK (CI)

Every push builds the port with the real NDK — [`.github/workflows/android.yml`](../.github/workflows/android.yml).
A successful run leaves the APKs in the `fpdc-apk` artifact of its *Build APK* job (download it from the run's
Artifacts section) and installs over the previous version (see *Installing an update* above).

The workflow has two jobs, and it does not run anything on an emulator — the port is tested on a real phone:

| job | what it does |
| --- | --- |
| `checks` | the host checks below: the GLES sources compile against the stub NDK headers, every launcher screen renders to PNG, the colour packing, the draw order and the *texture handle lifecycle* of the GLES backend are pinned by tests (`check_texlife.c`: a handle dies with the table it came from, freeing a stale one must not delete the atlas that took its slot - the black screen on the second launch - and the upscale really samples the framebuffer's texture - the garbled PIXEL mode), and a selftest taps through the launcher (PLAY, the tabs, a row, a switch, a slider), the credits window (both links really reach the platform), the update window and the LANGUAGE row |
| `apk` | `sdkmanager` installs the SDK/NDK/CMake, Gradle 8.7 builds `assembleDebug` (debug only: no release APK any more), then the APK is inspected: `lib/*/libfpdc.so` and `classes.dex` present and signed with the committed key (that is what makes an update install without an uninstall), and **each ABI it ships is required to be there** — `arm64-v8a`, `armeabi-v7a` (32-bit, checked to be an `ELF32` file) and `x86_64` |

## Verified / not verified

Runs on a plain Linux host without the NDK or a device:

```sh
make -C tools/android check      # syntax + twiddle + font + preview build
make -C tools/android preview    # every launcher/editor/in-game screen as PNG
```

`check` compiles the two GLES backends and the platform layer with `-Wall -Wextra` against the stub headers in
`tools/android/ndkstub/`, verifies `fp_untwiddle_offset()` against `tools/twiddle_disc.py` for 15 atlas shapes,
checks the generated 5×7 font, and builds the shell against the host UI backend.

Verified by CI: the NDK build of all three ABIs (each one required in the APK, `armeabi-v7a` checked to be a
32-bit ELF), both APKs carrying `libfpdc.so` and `classes.dex`, the signature being identical across builds,
and the host checks above.

**Verified on a phone by hand** (that is the point of the log): the launcher, the controls editor, the
settings, the import of the data package, touch and gamepad input, and the game itself. The host tests catch
the two whole classes of bug that only showed up on a phone so far: the GLES backend drew every rounded panel
*over* the text (so button labels were invisible), and a tap that started the launcher's PLAY button went to an
empty stub. `make -C tools/android check` runs both.

**Not verified by CI at all:** anything that needs a device — AAudio latency, the real touch feel, thermals,
and the GPU-specific rendering. That is why the log exists: `ADB`/`SHARE LOG FILE`, and the numbers in it
(window, dp, ui target, quads, GL errors) say what the renderer did on that phone.

## Why ETC2 is optional

The disc's atlases are 8-bit paletted. Uploading the index byte and looking the colour up in a 256×4 palette
texture costs 8 bpp — exactly what the Dreamcast used — is lossless, needs no CPU pass at load, and makes a
palette change a 1 KB upload. ETC2 would halve the memory (4 bpp) but throws the palette away: every atlas has
to be converted offline (and re-converted if the palette changes), and the picture gains ETC2's block
artefacts on the gradients the pixel-art look depends on. Since the renderer already supports both, ETC2 can be
added later for the handful of large background atlases where bandwidth might matter on a very weak GPU;
nothing else needs it.
