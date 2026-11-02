# Runtime Map: original Freedom Planet (Linux build, v1.21.5)

Build: `bin64/Chowdren` sha256 `ff514984…` (not stripped); title screen shows **v1.21.5** (same as the Windows install).
Tags: **RV** = runtime-verified (traced in the running game) · **SD** = static-derived (disassembly) · **INF** = inferred.

## Launch and navigation
| Item | Value | Tag |
|---|---|---|
| Headless launch | `tools/orig/start_orig.sh`: Xvfb `:93` 852×480, llvmpipe, dummy audio, writable copy `/home/claude/fprun` | RV |
| Display fix | The window is created 426×240 but the game renders 2× (852×480), so only the bottom-left quarter showed. Resize the window **once**, ~5 s after startup, to 852×480 at (0,0). Resizing earlier leaves a stale viewport (offset or 1× image). | RV |
| Audio files | A missing `Data/BGM`/`voices` ogg crashes the stream thread; silent placeholders with the same names work | RV |
| Menu route to DV1 | `tools/orig/route_dv1.sh`: frames 0 → 82 → 2 → 3 → 81 → 75 → 20. Keys: Return ×2 (skip intro), Return ×3 (Start Game, New File 1 Normal, Adventure), then `z` ×~70 to advance dialogue | RV |
| Frame indices | `Setting frame: N` = `Frames::on_frame_(N+1)_init`. **DV1 = 20** (`on_frame_21`), the stage entered after the intro scene (75) | RV |
| Restart stage | `GameManager::set_frame(manager=0x285ec60, 20)` @0x9fc8c0, called from gdb **outside** breakpoint handlers (calling inferior functions inside `stop()` hangs gdb) | RV |
| Save files | `file1..8.sav` absent → "Could not load data.array" (normal for a fresh install) | RV |

## Main loop and timing
| Item | Value | Tag |
|---|---|---|
| Loop | `GameManager::update()` → `platform_poll_events()` → … → one `Frame::update()` → draw. One logic step per call. Wall-clock time only feeds `FPSLimiter` and timers. | SD + RV (traces advance one logic frame per breakpoint) |
| Per-frame hook | `0x9fcb7a` (just after `platform_poll_events`) | RV |
| Input lag | A key set at frame f first changes state read at the start of frame f+2 | RV |

## Input
| Item | Value | Tag |
|---|---|---|
| Key list | `manager+0x88` count; entries from `manager+0x8c` (0x285ecec): {int SDL keycode, u8 state}. state 0 = pressed this frame, 1 = held, 2 = up | SD + RV |
| Accessors | `is_key_pressed` (state≠2, 15 call sites), `is_key_pressed_once` (state==0, 42 sites), joystick functions (30). `is_player_pressed` / bitmask at manager+0x1b4/0x1b8 are effectively unused | SD |
| Keycodes seen | Right 0x4000004F, Left 0x40000050, Down 0x40000051, Up 0x40000052, z 0x7A (jump, RV), x 0x78, c 0x63 (attack/special mapping: see traces) | RV/INF |
| Injection | `tools/orig/trace_orig.py` rewrites the key list at 0x9fcb7a each frame | RV |

## Object and memory layout
| Item | Value | Tag |
|---|---|---|
| FrameObject | x int @+0x8, y int @+0xc (layer-relative; layers all at 0 in DV1), alterables* @+0x20 | SD + RV |
| Alterables block (pool, 0x128 B) | 10 string slots, then **26 doubles (A–Z) @+0x50** | SD + RV |
| Active animation | s8 @+0xb0 (`Active::set_animation`) | SD |
| Global values | `manager+0x8` → doubles (only #3 changes: frame/time counter) | RV |
| Lilac objects | `Player_326` (control, vtable 0x20b6f20), `PlayerSkinSash_341` (sprite), `PlayerCharacterSensor_375`, **`PlayerMovementValues_253`** (vtable 0x20b2900) = movement state | RV |

## PlayerMovementValues_253 alterables (Lilac, DV1)
| Alt | Meaning | Value / behaviour | Tag |
|---|---|---|---|
| A | ? | 0 | — |
| B | **ground speed** on ground; **air x speed** in the air | — | RV |
| C | **top run speed** | 5 | RV (speed settles at 5.0128 on flat) |
| D | ? (max speed?) | 15 | INF |
| E | **acceleration** | 0.07375 px/f² | RV |
| F | **slope factor** | 0.203125: dB = −F·sin(O) when \|O\| > ~30° | RV |
| G | **air y speed** | — | RV |
| H | ? | 15 | — |
| I | **gravity** | 0.25 px/f² (G += I each air frame) | RV |
| J | ? | 0.84 | — |
| K | ? | 0 in run/curve traces | — |
| L | **on-ground flag** | 1 ground, 0 air | RV |
| M | state/action id? | 11 while pushing into a wall | INF |
| N | **raw surface angle** (degrees, CCW, 0/360 flat; 315 = 45° downhill to the right) | — | RV |
| O | **smoothed angle** used for physics (eases toward N) | — | RV |
| Q | facing | 1 right / −1 left | RV |
| W | ? | 0.75 normally, 5.5288 after hitting the wall | — |
| X, Y | **precise position** (sub-pixel); reference point ≈14 px above the feet | — | RV |

## Ground movement (from `run_right` trace)
- From rest, holding a direction: **+9·E (0.66375) per frame while |B| < 2**, then **+E (0.07375)** up to **C = 5**.
- At or above C while holding the direction: speed is kept (no friction). Downhill can push it above C (7.94 seen).
- Slope force only when the smoothed angle O is steeper than ~30°: **B −= F·sin(O)**, both downhill (speeds up) and uphill (slows).
- Movement per frame ≈ B along the surface (x advances ≈ B·cos(O)).
- Quarter-pipe: rides up to ~82° then leaves the lip. Launch values observed: B (air x) = 2.0, G = −5.97.
- In the air holding a direction: B += 2·E (0.1475)/frame; G += I (0.25)/frame.
- Landing: the original runs the ground-speed update on the landing frame itself (down_kick: lands at B 5 → 4.631 the same frame).
- Crouch stop: holding Down (no direction) on the ground, speed is zeroed on the frame it would drop under 2 (down_kick f119, M3).

## Jump (RV, `jump_hold`, `jump_tap`, `run_jump`)
- Jump frame: Lilac first moves along the ground with her current B, then velocity = B along the surface + 7 perpendicular to the **raw** angle N. No gravity that frame. (run_jump: N 355.914, B 5.013 → vx 5.499, vy −6.625. The smoothed O would give 5.396 / −6.71.)
- Each air frame: move, then air accel (2E), gravity (I = 0.25), then drag; releasing jump clamps rising speed to −3 after gravity.
- Air drag: **vx ×= 0.96875 while −4 < vy < 0**, tested after gravity (attack_up: vx 2 → 1.9375 on the frame vy goes −4 → −3.75).
- Apex from flat ground 101.5 px; airtime 57 frames; tap jump 46 px / 35 frames.

## Moves (RV unless noted). Keys: z = jump, x = attack, c = special
| Move | Input | State M / anim | Behaviour |
|---|---|---|---|
| Look up | Up, stopped | M2 / 11 | no movement |
| Crouch | Down, stopped (or landing slow with Down held) | M3 / 10 | no movement |
| Ground attack | x on ground | M20 / 26 | 22 frames; physics unchanged (no input) |
| Air attack | x in air | M20 / 25 | 22 frames; physics unchanged |
| Uppercut | Up + x on ground | M21 / 28 | stands 10 frames, then vx = 2·facing, vy = −7; state ends ~39 frames after start; jump cut does not apply |
| High uppercut | Up + x + z together | M21 / 28 | jumps during the 10-frame windup, then vy is reset to −7 (rises ~160 px total) |
| Crouch kick | Down + x on ground, stopped | M? / 30 | 18 frames |
| Down kick | Down + x in air | M19 / 7, then M0 / 19 | windup 15 frames at gravity 0.125 (jump ignored), then vy += 5, vx = 5·facing, normal gravity until landing. From the kick phase a Dragon Cyclone is allowed if not yet used this airtime |
| Dragon Cyclone (air) | z again in the air | M26 / 18 | vy = −3 (0 if Down held), gravity 0.1 (0.25 with Down); lasts **79 frames**, landing does not end it; once per airtime; can be cancelled into a down kick |
| Dragon Cyclone (ground) | Down + x while moving (gsp ≠ 0) | M26 / 18 | 79 frames with normal ground physics and steering; jump during it → vy −4.9 |
| Action entry | — | — | on the frame an action starts, the old state's physics run first, then the state switches ("move-then-switch") |
| Dragon Boost | hold c with a FULL meter | M5 / 7, then M5 / 13 | a tap does nothing; needs energy = 100. Holding: 30-frame charge (speed frozen; in the air one move frame, then hover), then auto-fires for 33 frames: straight 12, diagonals (±8, ±8); Up/Down alone give the facing-side diagonal; a down-diagonal boost that lands continues on the ground at 10. Slope force still applies. Sprite angle = direction of travel; left-facing boosts use the dir-16 frames rotated 180° |

## Animation selection (playerskinsash_341, RV indices)
Active animation id (byte at +0xb1; the old image-table labels said 17 for skid, 22 for fastrun and 16 for hurt, which were wrong): idle 0 · walk 1 (0 < |B| < 5) · run 2 (5 ≤ |B| < 9.5) · fastrun 23 (≥ 9.5) · jump 24 · skid 16 · lookup 11 · crouch 10 · attack 26 · airatk 25 · uppercut 28 · charge / down-kick windup 7 · down kick 19 · cyclone 18 · boost 13 · hurt 8 · defeat 38 · crouch kick 30 · spring 14 · hang 15. RE: skin animation by OTHERS.Y, events 2426-2470. The hair overlay (PlayerSkinSashHair_424) always has the skin's animation id, with its own frames and speeds, drawn behind at the same position and angle. Walk/run playback speeds are fitted (≈30 and ≈35+|B|), not read from code.

## Move overlay effects (RV `build/re/fx` probes: TRACE_ADDOBJ / sparkle positions; RE events 2043, 2834, 2844, 2845)
- Cyclone (M26): RingSparkle_142 every 6 frames from the first M26 frame at (int X, int Y) + rand 0..40 − 20 per axis; an invisible ShieldInstaShield_407 is created with it. Energy −0.25 per frame (state at frame start).
- Charge and boost (M5): RingSparkle every other frame at ±8. PlayerOthers D = 1, E = 29 during M5 (and for 29 frames after).
- The sparkles are drawn additively (Chowdren shader 0x20, as the shields and the strobes are): the twinkle has a dark
  ring that the additive pass adds almost nothing for, so an opaque draw turns it into a hard blue outline - the "white
  plus sign over the heroine" a phone report showed during a boost. `fx_draw` (player.c) sets the additive state now.
- Invincibility (Others D = 1; boost or monitor): Sparkle1_316…Sparkle4_400 orbit an anchor (int X, int Y − 4): 1/2 at radius 20, D +7°/frame, 180° apart; 3/4 at radius 10, +10°/frame. Position = anchor + r·(sin D, −cos D). Anchors follow Lilac every 3 frames (1/2) and 6 frames (3/4). Every 6 frames a Sparkle5_284 (anim 4, plays once) is left at each orbiting sparkle.

## Comparison harness
`tools/orig/run_all.sh` traces every `tests/orig/*.txt` in the original; `tools/orig/cmp_all.sh` runs the same inputs in the fpdc host build and checks key outcomes (apex, top speed, time to top, airtime, distance, stop timing). The **16 baseline sequences pass** (host-test vs original-trace); 46 of 71 sequences pass overall (see BUILD_AND_TEST_LOG for the failing ones and their first failing frames).

## Frame index table (RV: title card after `set_frame(N)` in the running original, `tools/orig/goto_frame.sh`)
`N` = "Setting frame: N" = `Frames::on_frame_(N+1)_init` = `build/frames/frame_(N+1).json`. Stage numbers on the cards depend on the character route, so only names are recorded. Screenshots: `build/orig/sel_N.png`.
| N | Content | N | Content |
|---|---|---|---|
| 13 | bonus room (dice) | 44–49 | Thermal Base (6 frames) |
| 14, 15 | Lilac's treehouse (hub, HUD) | 50–55 | Pangu Lagoon (6) |
| 16–19 | Aqua Tunnel (4) | 56–58 | Trap Hideout (3) |
| **20–22** | **Dragon Valley acts (card "Dragon Valley")** | 59–64 | Battle Glacier (6; 64 = boss arena) |
| 23 | Dragon Valley, no card (continuation/boss section, tree start) | 65–72 | Final Dreadnought rounds 1–4 (2 frames each) |
| 24–28 | Relic Maze (5) | 73 | "Hit the targets" mini-game |
| 29–34 | Fortune Night (6; 34 = boss) | 74 | Continue? screen |
| 35–38 | Sky Battalion (4) | 75–82 | story/scene rooms (dialogue) |
| 39–43 | Jade Creek (5) | 0–12, 83–86 | intro, menus, file select, map, credits (see `build/frame_summary.txt`) |
Note: after a few jumps between frames the original swapped the playable character (Milla/Carol appear on later cards): the character is a global, not part of the frame.

## Energy meter, petals, lives, crystals (RV; GUIEnergy_127 alt A, counters at +0xa0 / max +0xac)
- Meter 0..100. **Starts empty at every stage/act start** and refills **0.4/frame** (250 frames). Boost needs 100; drains **3/frame** while boosting (paused during the 4-frame enemy hitstop).
- Crystal +5 energy (enemy-drop crystal +10; the meter can briefly read 110). Petals heal one half-petal, add no energy, do nothing at full health.
- **No 8-second boost cooldown exists, and petals do not shorten one.** The boost is gated only by the meter (prompt §2C discrepancy).
- Petal counter max 14 half-petals (7 petals). Crystal counter counts DOWN from 200 (200 = a life). Lives start at 5.

## Damage and invulnerability (RV)
- Hurt (RE, event 2414): **B = 0.75·B − 2.2·Q, G = 0.75·G − 4.2** (Q = facing, so the knock-back is away from the facing), gravity 0.5, state M8 for 22 frames (skin alt O counts 1..22). Damage = mult × source / 5 half-petals (Turretus pellet 10 → 2 on Normal, 4 on Hard; Casual none). Sound 25 (+17 logged, + voice).
- Post-hit invulnerability (RE + RV): **60 frames**, PlayerOthers_126.E set to 60 and counted down 1 per frame; collision handlers ignore Lilac while E > 0.

## Springs (RV, DV1 = on_frame_21; traces `tests/orig/spring_*.txt`)
| Spring | Effect |
|---|---|
| yellowspringup (32×16) | fires on the frame the feet reach its top edge; feet put at top + 0.25; vy = −10.75 (vx kept); state M40, skin anim 14 while rising, then the fall anim; normal gravity; lands back on it → same bounce |
| yellowspringupleft/right (32×32) | same top-edge rule (feet exactly on the top); (∓9 / ±9, −8.75); normal air drag below |vy| 4 |
| yellowspringright/left (16×32, on walls) | ground speed ±10, and that frame's friction already applied (9.63125); **input ignored for 4 more frames**; player half-width against it 16.8–19.4 (not the ±7 body box) |
Spring sound id 5 (played on contact).

## Skid / turning rule (RV, `skid`, `spring_yellow_right`)
Input against the motion: if it turns Lilac round, that frame is friction (5E / 9E) + acceleration toward the input, and the skid state starts next frame (−0.5/frame, anim 16, until stopped). If she already faces the input (e.g. thrown backwards by a spring), there is no skid, just friction + acceleration every frame (6E above speed 2, ×9 rules below).

## Landing (RE/RV)
Ground is detected by PlayerSensorBottomBig_334 (11×11 at floor Yref + 13): surface rows up to **floor(feet) + 4** count, then the slope loops move Lilac onto the surface. Landing speed (event 1591): **B = B − G·0.84·sin(N)** for 22° < N < 338°, with N from the landing-angle routine (not ported yet; ours measures the angle differently). A boost that lands keeps its speed. Falling speed tops out at **15.25** (event 1577 clamps G to 15 before gravity).

## Ceiling (RV, `spring_yellow_upleft` f52)
On the frame the head reaches a ceiling, vy becomes 0 at once (no gravity that frame; air drag still applies).

## Sound IDs (Media::play_id 0xa4e650 / play(string) 0xa4e2c0, logged by the tracer to `<trace>.snd`)
8 jump · 9 boost charge · 53 boost fire · 21 attack swing (7 frames in) · 288 + 174 cyclone · 405 kick windup · 176 + 396 kick launch · 25 + one of 80/81/82 Lilac hurt (17 also on projectile hits) · 307 + 12 monitor break · 13 rock break · 28 Turretus shot · 5 spring · 27 crystal pickup · 4 pickup (ambiguous) · 100 Press Start · 64 menu move · 63 confirm · 56 begin.

## Music (RV: `loop_play_music_<n>_<frame>` / `on_frame_N` handlers)
m_title (title frames) · m_menu2 (hub: title menu, files, options; RV) · m_menu1 (Time Attack character select + stage select) · m_results (Classic/Adventure character select; RV) · m_stage1 (DV frame index 20) · m_stage1b (21–22) · m_boss (23 = DV4; RE: `Frames::loop_play_music_4_23` string) · m_stage2 (24, Relic Maze 1) · m_invincibility (invincibility monitor) · m_1up (1-up monitor) · m_clear → m_results (results, Time Attack trace: m_clear at act end, m_results 150 frames later). BGM loops whole-file: ChowdrenAudio has no loop points (SD). `m_stage2a.ogg` is referenced but absent in this build.

## Front end (RV positions in the original frames; 426×240 view)
- Frame indices (RE, member maps): 2 Press Start (camera 107,0) · 3 hub 1440×480: title menu camera (43,0), file select (43,240), options (523,0) · 5 Time Attack stage select · 6 character select · 7 dispatcher · 8 stage card · 10 gallery · 11 finished-file stage select · 12 mahjong. (Session 5 listed the stage select as 11: corrected, 11 is the finished-file variant.)
- Title cursor x 138, rows y 131/151/171/191/211 (START GAME, TIME ATTACK, GALLERY, OPTIONS, QUIT).
- File select: cursor x 170 at y 160/184/216; slots centred at 213, 120 px pitch, wrapping; hint box fileback frame 0 at (93,16), text at (101,24)/(101,44), wrapped at 26 chars. Strings: "Press START to begin a new game.", "Press LEFT or RIGHT to adjust the DIFFICULTY.", "Select RETURN to pick another file slot.".
- Options: cursor x 90 at y 72/128/160/184, OKAY x 170 y 216; values right-aligned (last digit x 337, y 120 music / 176 sensitivity, 14 px pitch). Difficulty dirs 0 HARD, 1 NORMAL, 2 EASY, 3 CASUAL. Defaults Normal, music 100 %, sensitivity 70 %.
- Footer " A: Select ... B: Return". Font Data/font.bmp, 8×20 cells. Transition: 32 px tiles shrinking over 6 frames (stagger INFERRED).
- Stage select: stagename_138 / stageicon1_139 frames 0 Aqua Tunnel, 1 Dragon Valley, 2 Relic Maze, 3 Fortune Night.
- Developer level select (port-only): **only** L+R within 2 s on the title screen (Press Start or title menu).

## HUD (RV template match on docs/reference/hud_dv1.png, 426×240)
Meter frame 3784 at (8,8); fill 7439 at (16,10) scaled by energy/100 and drawn on top; red end cap 12760 at low health; shine loop (guienergy anim 1) when full. Petals guirings frame = hp at (8,19). Lives icon 311 at (128,8), digits x 144/154. Crystal digits x 176/186/196. Clock 4564 at (322,0); timer digits 343/353, 369/379, 395/405 at y 8 (mm:ss:cc).

## Stage card, pause, results
- Card: STAGE text final y 159, name y 185, centred x 213 (10 fps capture; rise/fade timings approximate).
- Pause (RE, events 4878-4881/4296-4305): GUIPause at (214,101), art frame 3 = Resume/Restart/Quit, 4 = Restart greyed (lives ≤ 1); Crystal cursor at (166, 141 + 24·row); 50-frame wipe for Restart/Quit. Sounds 63 open, 64 move, 56 confirm.
- Act transition DV1 → DV2: fade + card, no results, timer continues.
- Results assets: 8005 "Stage Complete!" (320×48; build-in from levelcardactclear1 anim 7), 955 "Results" banner, 10860 panel (180×48), 10837 rows (148×40: crystal icon, TIME, colons), totalbonus digits 2794…, small digits 1922…. **Layout INFERRED**: the original's post-boss results were not observed (killing the Serpentine via memory failed, time-boxed).

## Recovered from the executable (session 4; details and event numbers in RE_NOTES.md)
- **Camera:** MasterCamera_408: ±16 px horizontal dead zone (the view snaps after a teleport), grounded landing ease 4 px/frame, air dead zone −48…+16, look-ahead up to 64 px at speed ≥ 5 (2.5/frame while boosting), look up/down 96 px after 120 frames, display clamp to the frame. Replaces the reconstructed 0.15 easing.
- **Turretus:** idle, aims in direction 15/1 (11.25° up), timer E counts while within 200 px of the view, shoots at E > 60 then E = −40; pellet released at shoot-animation frame 7 from the action point (±18, −14). **EnemyPellet:** PinballMovement 5 px/frame, gravity 0.25 px/frame², floor stepping (exact).
- **Animation timing:** a frame advances when the counter goes above 100 (not at 100); changing animation keeps the counter.
- **Angle smoothing:** k = max(0.175, |B|/15) within 45°, snap beyond. **Slope force:** N in 35…325°, ×0.38 below speed 5, none when stopped.
- **Precision:** the original floors B, G, N to 4 decimals every frame in doubles; the port now uses doubles for Lilac's physics.
- **Handlebar:** grabbed by the top-big sensor while falling; Lilac hangs at (sensor x, sensor y + 32).
- **Results screen:** located (DV4 events 4560–4589, `loop_tally_19`), not ported; the port's layout is still INFERRED.

## Monitors and items (RE events 2543, 1288-1291, 2527-2531, 2632-2670)
- Crystal: reach X ±19, Y −39..+19 of (int X, int Y); not while hurt (M8/M9); energy +5 (max 100); crystal counter −1; sound 27; RingSparkle.
- Petal: reach Y −31..+19; sound 37; flies to the HUD counter; +1 half-petal (+2 on Casual), sound 4, two hearts. No pickup at full health (RV).
- MonitorCrystals throws 7 crystals (loop_spawn_crystals 1323), MonitorRings throws petals, Monitor1up gives a life, MonitorInvincibility sets Others D = 1, E = 1800 and plays m_invincibility. Throw arcs and the 1-up jingle length are INFERRED.

## Loading screen and audio (port design, host-test + emulator)
Every level, front-end and boot load runs behind a black loading screen with ObjectCrystal_317's spin bottom-right. The music stream and every SFX channel are stopped for the whole load, and the next track starts after it. Root cause of the doubled or stuttering music on the Dreamcast: the AICA loops the stream ring buffer in hardware (`chan->loop = 1`), and blocking GD-ROM loads starve the refill thread, so stale halves replay. Also fixed: the stream volume was set before `snd_stream_start`, which forces volume 255.

## Front-end screens in the port (session 6)
| screen | original frame | port | sounds | from the original | INFERRED / not ported |
|---|---|---|---|---|---|
| Press Start | 2 | frontend.c FE_PRESS | 100 | events 307-330, 1-frame input latency | – |
| Title menu | 3 (cam 43,0) | fe_hub.c title_events | 63/64 | 380-392, console layout | QUIT hidden (console) |
| Gallery | 10 | fe_gallery.inc FE_GAL | 64/63/32/40/443/56/100 | spec_gallery (992-1139), RV t_gal1-6 | see "Session 15: Gallery" |
| File / mode select | 3 (cam 43,240) | file_events, delete_events | 63/64/56 | 393-437 | Adventure skips the cutscene; continue starts DV1 |
| Options | 3 (cam 523,0) | options_events | 63/64 | 464-511 | Controls / screen size / sensitivity hidden (console); SFX volume display-only on host |
| Character select | 6 | frontend.c FE_CHSEL | 63, 220/221/186, 56 | spec_charselect | Lilac, Carol (session 10) and Milla (session 11) playable |
| TA stage select | 5 | fe_stsel.inc | 63/64/56 | spec_stageselect | only Dragon Valley startable; BG parallax drift not ported; confirm 3 frames late |
| Pause | 20 (DV1) | game.c pause_* | 63/64/56 | 4878-4881, 4296-4305, 3551 | QUIT from Classic → hub |

## Session 7: ladders, platforms, Ganseki, push (port: src/characters/player.c, src/game/objects.c)
- States: ST_PUSH 12, ST_LADDER 13 (M46), ST_LADDERIN 14 (M25). Anims: push, climb, climbdown, climbover, catch.
- Object types: OT_LADDERTOP 30, OT_GANSEKI 31, OT_PLAT_VALLEY 32, OT_PLAT_MOVE2 33, OT_PLAT_MOVE3 34, OT_BRIDGE 35, OT_LOG 36.
- The camera follows Lilac's Y on the ladder; cam.M4 holds during the enter-from-top.
- Hit-stop: simplified (whole player update skipped while it counts down).
- Mirrored dir-16 animations are no longer stored; `set_anim` draws the right-facing anim flipped (draw_flip / hair_flip).
- Loading: packs per screen in RAM, released at loading_end; loose files only loading.* and vmu_*.

## Session 8: bosses, chase, results, parallax, keystones, roll/tunnels (port: src/game/bosses.c, serpentine.c, chase.c, results.c, parallax.c, gimmicks.c)
- States: ST_CORK 15 (M45), ST_ROLL 16 (M6). Roll animation = skin anim 7 (2438), speed int(|B|·5 + 25).
- Object types added: OT_CHUNK 138, OT_WF_* 139-143, OT_BBOULDER 144, OT_KEYSTONE 146, OT_SPIRAL_FRONT 147,
  OT_FRONTROCK/FRONTTREE/TREEFRONT 148-150; spinning logs (115/116) and PushableBlock1 (98) now active.
- Original traces recorded this session (scripts in tests/orig_extra_*.txt, CSVs in build/orig_traces/extra_*.csv):
  tredd 1-3, snake_t1, dv4_chase, dv4_results, dv4_boss, spiral_dv1 (top ride), spiralb_dv1 (bottom ride + reversal),
  balltunnel_dv3, pblock_dv2. Host TP lands 2-4 frames before the original's; tests align by that offset.
- Matches: Tredd 1100/1100 frames; DV2 snake 271/271; results timings 72/292/441; spiral bottom ride, DV3 ball tunnel and
  the pushable block exact (X, B per frame); spiral top ride exact up to input alignment.
- Camera bounds: boss mode (U = 1, 2862-2868 ease 2 px/frame), chase mode 3 (minimum C), cinematic (5590).
- Object layer drawing: objects_draw(pass) 0 = layers ≤ 1, 1 = layer 2, 2 = layers ≥ 3; order pieces 0-1, objects 0,
  pieces 2, objects 1, Lilac, spiral front, pieces 3, objects 2, pieces 4-7.
- Teleport caveat (tracer): sensors are clamped to the camera bounds, so a TP far from the start region can push Lilac out
  of the "ground" every frame (DV2 logs at x 10240 could not be reached that way).
- Emulator (Flycast, developer select, 16:9): DV1-DV4 load and draw; pvr free after load 2046 / 1211 / 1326 / 1144 KB.

## Session 9: DC perf log and frame budget (port: src/platform/plat_dc.c, fp.h PROF)
Every 600 frames the DC build prints `perf:` and `prof(avg/max us):` with per-section average/maximum microseconds
(par, plat = objects_platforms, player, layers, obj = objects_update, gimmick = shields/water/pblocks/spinlogs/tunnel/
cork, cam, res, draw = game_draw incl. wait, submit = list submission, wait = pvr_wait_ready, mus = music refills on
the stream thread), `frame_avg` and `vblanks N per 600 frames` (pvr_get_vbl_count: 600 = no frame missed), then up to 8
missed-vblank frames with their own update/draw split. Times are timer_us_gettime64 (emulated time in Flycast).
Demo discs print `demo f` every 600 frames (console output is slow; the report frame is excluded from the pacing).

## Session 10: Carol (port: src/characters/carol.inc, carol_bike.inc, player.c; src/game/combat.c, game.c)
- Characters: CHR_LILAC 0, CHR_CAROL 2, CHR_CAROL_BIKE 3 (= the original's MV.A). Host `FP_CHAR=<id>`, demo discs
  `CHAR <id>`; character select sets G.character; a file slot saved on the bike starts on foot (hub).
- States: ST_CLAW 17 (M20), ST_WILDKICK 18 (M44), ST_WALL 19 (M47), ST_PAD 20 (M48), ST_BIKEAIR 21 (M32); the pounce
  is ST_NORMAL with cY = 24, the roll is ST_ROLL (M6). `player_orig_m()` maps back to the original M for traces.
- Player fields: cY (OTHERS.Y), cL/cA (PSC.L / PSC.A: kick counter, pad count, combo index), bikeB (BSK.B), bikeC
  (bike hit points 3), c_pavO (PAV_B.O), pad* (pad ride), item_ovr (pre-move pick-up position).
- Objects: OT_FUEL 158 (jerry can), OT_PBIKE 159 (parked bike); pads are created by pads_spawn() from per-level
  tables (PADS_DV2/DV3/DV4) in objects_spawn.
- Banks: carol2 (skin, palette pal2c.pal replaces bank 2), caroltail (overlay), carolbike (BSK + pbike + effects);
  carol2 and carolbike have second atlases (`_2.tex`).
- Event order per frame (Carol): energy → movement (bike_phys after water) → state switch (claw/kick/wall/bikeair/pad)
  → fuel_events → pads_events → Carol group (timers, bike_events, 2432, roll ball, Y9/Y13) → animation (cY → anim,
  tail, PSC) → engine update.
- Sounds: loaded per character at level load (`snd_group_load`); bike engine through `snd_loop_start/set/end`
  (DC: snd_sfx_play_ex looping + AICA_CH_CMD_UPDATE for volume/frequency).
- Traces: tests/orig_carol/*.txt (c_ on foot DV1, b_ on the bike DV1, c_jumppad_dv2 DV2), CSVs build/orig_traces/carol,
  compared by tools/orig/cmp_carol.py (V=1 prints the first mismatching frame per field). Host CSV columns added:
  M, cY, cA, cL, cS, aname, inv, hafr, sacc, hacc, rev; host f-line adds chr, bike, bc, oy.

## Session 11: Milla (port: src/characters/milla.inc, player.c; combat.c 2742-2745; objects.c rocks / monitors)
- Character id CHR_MILLA 4 (host `FP_CHAR=4`, demo `CHAR 4`); select cursor 3, voice 186. Classic / Adventure start at
  DV1 in the port (the original sends Milla to Aqua Tunnel 1, frame 16, which is not ported: D-040).
- States: ST_MATK 22 (M20), ST_FLOAT 23 (M22), ST_DIG 24 (M27), ST_MCARRY 25 (M30), ST_MSHIELD 26 (M31);
  `player_orig_m()` maps them back. OTHERS.Y is `cY` (shared with Carol).
- Player fields: mA/mB/mC/mD (dig power, progress, reward roll, zone), mE (float meter), mF (holding: 2 cube), mG (cube
  charge), mH (attack cool-down), mN (float counter), mfl1/mfl10, nol (MV.V = 0), mC4 (MV.C = 4), skinI.
- The cube: a static `mcube_t` in milla.inc (not in G.o): drawn after the skin, ticked and collided at the frame start
  (update_objects + test_collisions_20), its group 0x21a67 after the state groups. Hooks: `player_cube_hits /
  _breaks_rock / _opens_monitor / _damage` for combat.c and objects.c.
- Frame order: cube tick + collisions -> movement (milla_normal / milla_move) -> 1665 -> clamp -> 1723-1725 (grounded
  M0, after the clamp) -> 0x21a67 -> M20 / M22 / M27 / M30 / M31 groups (each only while M is still its state) -> 2414
  -> animation (index from OTHERS.Y, direction 0/16 - F, nearest existing direction, tail with its own counter).
- Banks: milla2 (skin + dig mound + HUD panel `hudm`, ACT1 action points, palette pal2m.pal), millatail (4 bpp),
  millafx (PlayerReflect_463, 4 bpp) in G.lilac / G.hair / G.bike.
- Host: f-line adds rA rx ry me mh mm; CSV adds mE, mF, mG, rA, rx, ry, rC, rF, ranim, mdir. Script commands
  `<f> DIGZONE v` (MILLA.D) and `<f> SETV others E v` (invulnerability, same line as the tracer's).
- Traces: tests/orig_milla/*.txt (DV1; m_dig_at1 Aqua Tunnel, FRAME=16), CSVs build/orig_traces/milla, compared by
  tools/orig/cmp_milla.py (FR=295 for the 300-frame traces). Teleports far from the start can fail in the original
  (camera bounds): use spots near the DV1 start or at x 5430 / 6740.

## Session 12: Aqua Tunnel (port: src/game/aqua.c, aqua_gim.c, aqua_boss.c; tools/import_frame.py; tests/test_aqua.py)
- Levels at1..at4 = original frame index 16..19 (`tools/build_level.sh 17 at1 converted2` … `20 at4`). AT layers map
  2→1, 3→2, 4→3 (D-045). Per-level banks at<n>_ens hold every AT class (AT_ENS in import_frame.py; at1/at4 split into
  two atlases).
- Routes: Milla's Classic / Continue load at1; Time Attack icon 6 (AT) replaces DV for her (fe_stsel.inc
  `stage_playable`, RE 623-625); records per stage index (`stage_rec_index`: AT 0, DV 1).
- Object types: 160-182 (dig spot, enemies, jets, jelly, springs, bomb / crate), 183-213 (gimmicks B: ABPLAT, COLLAPSE,
  SDOOR / SDOOR2 / APLATE / WPLATE_AT (AT retypes of SwitchDoor / PressurePlate / WallPlate), MBLOCK, CRUSH_V /
  CRUSH_VIS / CRUSH_H / CRUSH_H2, PIPE / PIPEHUB / PIPECOG, BEAM, AXEL1/2, AXMAKER1/2, SWSPAWN1/2 → SWBASE / SWCHAIN /
  SWPLAT, SCROLLBRICK, SLIMEFLOOR / SLIMEF_VIS, RAFT, TREEOVL, FISHSTATUE), 214-227 (bosses: IW_HEAD → IW_HEADF at
  emergence (enemy), IW_SEG, IW_BASE, CH_HEAD / TAIL / BALL (enemy) / MOUTH, SN_HEAD / SEG / MOUTH, ACID, ROCK_AT,
  BUBBLE).
- Frame order in an AT act: objects_platforms (no-op in AT) → aqua_pre (dig zones) → player_update → objects_update
  (combat, shared enemy events) → aqua_update: bursts, bombs, jelly, bricks, **aqua_gim_post** (path 3283, carry 4061,
  swings, collapse, plates / doors, Milla block, wall plates + pipes, beam, crushers, axels, slime / rafts, dip),
  **aqua_boss_update** (worm / Chomper / snake, rocks, acid), fireflies, slimes, burst timers, whisps, jets,
  checkpoints, boss camera, jet maker, dig.
- Rotated object masks (o->ang): obj_px / rows_hit_rot in objects.c serve the sensors, obj_rect_hit and obj_obj_hit.
- Player: ST_SHAKE (M12), ST_HELD (M43, the Chomper's mouth), `tint` (OTHERS.R, green 0x80ff80 while > 0).
- Results: `results_begin(2)` = AT4 (timer frozen at the kill, Milla stands, raft exit 4588-4592, wipe at x 7024).
- Sounds: stage group SND_AT in game.c (D-047).
- Host: `FP_TRACK=type:spawnx` (spawnx -1 = any; 214 also matches 227), `FP_AT_GT`, `FP_AT_PRERUN`; script commands
  `<f> BOSSHP v` (AT boss HP). cmp_at.py writes the raw host output to build/fpdc_traces/at_<name>.out; `XTRACK=` adds
  port objects to it.

## Session 13: level table, reusable objects, switches, registry, save v3 (port: src/game/levels.c, switches.c, objdefs.c, save.c)
- **src/game/levels.c** — `level_def_t` per act (fp.h): dir, original frame index, stage index s (0 AT, 1 DV, ...; the
  save / Time Attack index), act / nacts, music (+ after checkpoint 10), water line, walk-in, exit rule (exit_max_y),
  edge results, AT axel / pipe values, pads, dig zones, plate rule / switch pass, monitor hop, stage sound group,
  checkpoints, boss camera lock, act-start item sets, camera bounds, start rule (checkpoints), start fall, card ids.
  API: `level_def(dir)`, `level_cur()`, `level_next(dir)` (NULL after the last act), `level_first_of_stage(s)`,
  `level_route_start(character)`; stage flow shared by every level: `level_flow_spawn()` (item sets),
  `level_flow_frame(p)` (checkpoints, boss camera), `level_camera_bounds()`, `level_release_camera(keep)`,
  `level_cam_locked()`. Readers: game.c (music, act end / next act, walk-in, start rule, stage sounds), water.c,
  results.c (edge results), gimmicks.c (spinning-log line), carol_bike.inc (pads), aqua.c (dig zones, AT stage
  script = stage 0), aqua_gim.c (axels, pipes, swings' water line), monitors.c, switches.c, frontend / fe_stsel (routes).
- **src/game/objdefs.c** — `objdef(type)`: registry flags OD_SOLID / OD_OBSTACLE / OD_NOEDGE (qualifier groups,
  obj_in_group) and OD_BOXCOL (image-box collision: sensors in objects.c, aqua_gim is_box), and the spawn sprite (the
  old EA table). One row per engine type, ranges as base + k. OBJECTS.md is generated from it.
- **src/game/switches.c** — pressure plates (OT_PPLATE, rule from level data), the Milla block, SwitchDoor 997/1066
  (OT_DOOR), 770 (OT_SDOOR), the AT1 gate (OT_SDOOR2); `switches_update(p, late)` runs in the platform block (DV,
  late 0) or in aqua_gim_post (AT, late 1).
- **Presence:** `objects_have(type)` / `objects_have_from(type)` (objects.c): on-map placed types of the level, set at
  spawn. The Aqua Tunnel passes run in any level with objects from OT_DIGSPOT up; tunnels (q27) and spinning logs follow
  their objects.
- **Engine types added:** 228 OT_ABPLAT2 (AquaBrickPlatform_803), 229 / 230 OT_AXEL1P / OT_AXEL2P (845 / 846), 231
  OT_SWSPAWN1B (848); importer types 210 (SwitchDoor_770), 212 (SwitchWallPlate_831) and 188 (LevelCrusher1_847)
  directly. 211 (OT_APLATE) is unused.
- **Frame order in an act (every level):** objects_platforms (DV platform passes, switches with switch_late 0) →
  aqua_pre (AT2 intro, dig zones) → player_update → objects_update → aqua_update (skipped without AT-family objects,
  then only level_flow_frame): bursts, bombs, jelly, bricks, aqua_gim_post (path, carry, swings, collapse, switches
  late 1, pipes, beam, crushers, axels, rafts, dip), the AT bosses (stage 0 only), fireflies, slimes, timers, whisps,
  jets, level_flow_frame, jet maker, dig.
- **Save v3** (save.c, fp.h): options; file[8] {used, character, mode, last_stage, lives, shield, counter, stage[15]
  {time ms, crystals, deaths}}; rec[15][4] {time ms, crystals}; progress[4] (s + 1); cards (ids 1..100). Totals =
  sums. Writes: `game_save_stage_end(time_ms)` at the results' D = 150 (and a last act without results) →
  `save_stage_end()`; cards at each act change (`cards_commit`). New game: `G.file_new` replaces the slot at the first
  stage end. Deaths per stage: `G.stage_deaths` (+1 per death restart).
- **UI (ui.c / ui.h):** kind 2 = solid quick backdrop (w, h, colours); kind 3 = translucent layer of the object before
  it (alpha in the header pad byte; it copies the owner's position, frame, visibility, tiling); `tilew` (preview strips
  repeat across the window), `tileh` (the checker layer wraps vertically).
- **Renderer (render_pvr.c):** translucent-list headers use PVR_TXRENV_MODULATEALPHA (D-060). objects.c `obj_obj_hit`
  computes each rotated object's cos / sin once per test (the DV4 boulder vs the tunnel walls).
- **Host:** `<f> LOAD <dir>` (a fresh start of another level in the same run), `FP_FRAME_US=n` (frames whose update exceeds n us), `FP_CARD_DBG` (prints the card images of
  each load), `FP_DUMP_OBJDEFS` (registry per type), `FP_TA` / `FP_FILE` / `FP_FILE_NEW` (save tests).
- **Tests:** tests/test_levels.py (cards, load orders, restart, hang animations, results panels, registry vs importer,
  next acts, checkpoint), tests/test_saves.py (records, files, progress, cards, conversion, CRC).

## Session 14: Relic Maze (port: src/game/relic.c, relic_gima.c, relic_gimb.c, relic_boss.c, rm_claw.c, rm_mantalith.c; levels.c, game.c)
- **Level rows (levels.c):** rm1-rm5 = frames 24-28, stage 2, cards 11-20, music m_stage2 / m_stage2b. Row fields added
  this session: `exits` (exit_t: edge, Y band, target, walk-off B), `entries` (entry_t by source act), `death_to`,
  `flag1_y`, `crystals_once`, `wplate_cyclone`, `gates` / `flagclears`, `breakout` / `breakout_flag`, `rooms`
  (camroom_t: camera bounds per room), `warps` (warp_t), `walkin_purge`, `rmg` (pillar zones, pads base, door return,
  basement x), `pads`, `nobike`, `plate_rule` 2 (radio). SFX_RM = the RM stage sound group (D-047 / D-063).
- **G fields:** `stage_flags` (ML 0x1 / 0x2 / 0x4), `exit_to`, `exit_b`, `walk_b`, `keep_g4`, `stage_carry`,
  `prev_dir` (the act just left, chooses the entry), `input_lock`, `warp`, `cam_nF`.
- **Frame order (game.c):** objects_platforms → aqua_pre → player_update (inside objects_sense_snapshot(1/0)) →
  layers → walk-in / exit walk → objects_update (relic_gima pre / post, golems, rocks, iris purge via relic_frame) →
  aqua_update → relic_frame → relic_gimb_frame → rm_claw_frame; Mantalith via rm_mantalith_frame / _draw hooks. Exits
  and warps are checked after the update; at clear_t == 255 a warp or next_act runs.
- **Type ranges:** 232-245 enemies, 246-292 acts 1-2, 300-336 acts 3-5 (member index in lvobj anim), 350-371 golems
  and markers, 400-449 Claw, 450-499 Mantalith; MAX_OT 512 (presence bits, registry, group tables).
- **Results:** results_begin_boss (path 3, any boss: stage time stopped at the kill), results_tally_done /
  results_frames / results_script_input / results_request_end; voice_play_once (two slots, freed at the stage end).
- **Front end:** Continue starts the stage after the file's last completed stage with its lives / shield / crystal
  counter (frame 8 routing); the TA list gets Relic Maze from the level table (stage_playable) with the progress lock.
- **objects.c helpers (D-067):** objects_sense_snapshot(on) (packed sensor prefilter), objects_alive_type(t)
  (superset of alive types, rebuilt on slot changes and each objects_update).
- **Host:** `FP_TRACK=type:spawnx` (now tm / vx / vy), BOSSHP for golems, Claw hitbox and Mantalith head / arms,
  `FP_DBG_OBJS`, `FP_TRACE_SND`, `FP_DBG_BG`. Tests: tests/test_rm_gimmicks_b.py, test_rm_claw.py, test_rm_mantalith.py,
  RM cases in test_gameplay / test_aqua.

## Session 15: Gallery (port: src/game/fe_gallery.inc, save.c v4)
- **Route:** title menu GALLERY (hub wipe, sound 56) -> frame index 10 (m_results, tile fade-in 14x8 from (96,0)) -> RETURN
  (sound 56, wipe, F > 50) -> title menu (m_menu2). View fixed at camera left 107; the scroll group eases 1/8 per frame to
  -(D + 1) x 30 (cards) / x 25 (achievements); Return_110 follows the header at y - 24 (RV y 52).
- **Pages (header Left / Right, wrap):** CARDS (10x10, rows DV RM AT FN SB JC TB TH PL BG = ids 10r+1..10r+10, row icon frames
  1 2 0 3 4 5 6 8 7 9 or the padlock 15), ACHIEVEMENTS (8x7, frame k when announced, else "?"), DATA (four delete rows, YES/NO
  panel starting on NO, applied when leaving).
- **Cards:** dir 0 grey star (not taken), 1 face down (shimmer frame (t/3 + id/2) mod 40, held after 10), 2 padlock (type 3
  without achievement 1-3), 3 note / 4 speaker / 5 cookie (opened). A on face down: sound 32, flip frame B mod 11, sparkle
  spiral, at B = 50 opened (save 2), sound 40, white burst, selector back at B > 100. A on an opened card: music
  (`bgm/<file>.adp`, type 1 once, 0 / 3 loop) or a clip (`gal/<file>.wav`, its WAV length = busy) on "channel 31" (m_results
  does not come back), again = stop; art = `gal/art_NN` at (322,0) on top, A closes (443). C on an opened card: Close
  Card (443, face down, save 1). Card 68 shows the bird guard while it plays.
- **Text box (120,8):** 50 columns, 20 px lines at x 13 / y 8: label + hint, a hint alone on line 1 (RV), name +
  description, or the DATA warning. Footer "UP/DOWN: Scroll" (13,216) / "B: Return" (341,216).
- **Entry Card Collector (RE 992):** rows with all 10 cards and achievement 23+r == 0 -> 1, save; the "UNLOCKED" overlay
  (3 per page, sound 100, 150 frames or A after 30; INFERRED layout, the original's frame 9 is not ported) sets them to 2 and
  saves again.
- **Leaving:** deletes (times / crystals of every stage and character, cards + opened, achievements except 42) then one save
  write, only when something changed in the gallery (INFERRED: the original saves unconditionally).
- **Save v4** (save.c, fp.h): v3 + `cards_open[13]` (records x20 == 2) + `ach[56]` (records x2 y1..56, 0 / 1 / 2). 1361 bytes
  (6 VMU blocks with the header and the 3-frame icon). v3 and older load with taken cards face down and no achievements;
  a short, long or CRC-damaged file is ignored and kept until the next deliberate save. Card pickups only set the taken bit
  (pickup = max(old, 1)). Title cheat: progress, all 100 cards (face down) and achievements 1-3 = 2.
- **Host trace:** `gal ...` lines (entry dumps of cards / dirs / achievements / rows, card writes, plays, art load / free, marks,
  deletes, saves); tests/test_gallery.py.


## Session 15: fixes and cheat (gallery: see "Session 15: Gallery" above)
- **objdefs.c OD_HIDDEN:** placed objects (objects_spawn) and relic_gima.c runtime spawns start with o->hidden = 1
  (collision unchanged). Types: OT_COLFRONT, OT_WR_BARRIER, OT_SPIRAL_FRONT, OT_SPINLOG_MASK.
- **objects.c:** the bank-extent caches (8-entry and per-slot) are cleared when the objects reset (G.ens / G.objs keep
  their address across levels).
- **relic_gima.c gates():** create gates use a per-gate bit (`gates_created >> k`) and wait for PX > gate x + 28.
- **frontend.c:** options (difficulty, music volume, letterbox) are taken from SV only on the first fe_load of the boot.
  Cheat: cheat_step() on FE_PRESS and on the hub title menu (hub_on_title), cheat_music() returns to m_title /
  m_menu2 after 240 frames; save.c save_unlock_all() (progress = 15 for 4 slots, 100 cards, achievements 1-3 = 2).
- **game.c:** HUD life icon index 17 (hud_icon_cb, image 395) for Carol on foot and on the bike; VMU life icon 2.
- **Host:** `<f> FLAGS v` sets G.stage_flags. Tests: test_frontend cheat_* (sequence, timeout, reset, title menu, save
  bytes), test_gallery.py.

## Session 16: achievements (spec build/re/spec_achievements.md; port save.c, results.c, combat.c, fe_gallery.inc)
- **Helper (save.c):** `ach_earn(k)` = records x2 y(k) 0 -> 1 (RE "set 1" / "max 1": an announced 2 is kept); no VMU write.
  `ach_pending()` = any achievement at 1. Persisted by the existing writes: the results' D = 150 save (5394) and the pop-up.
- **Hooks (RE event, port place):**
  - 10-17 `ach_boss_finish()` (combat.c) on each frame of the boss finish moment: DV4 serpentine.c after 5531
    (`S4.K == 0 && S4.hG == 1`, hitbox defeated), AT4 aqua_boss.c after 4642 (`O == 1`), RM5 rm_mantalith.c
    defeat_events() at head `I == 2`. It reads MV.A (`G.character`, 3 on the bike), MV.M (`player_orig_m`), a live OT_SHIELD
    with direction 14 / 30 (Carol's big slash) and Milla's cube (`player_cube_state()`, `player_cube_beam()` = A 5 + anim 5).
  - 42 at the same RM5 moment (7118); 36 rm_mantalith.c intro_events() (6900: both arm hitboxes A <= 0 while the intro
    group runs); 35 relic_boss.c after the 7116 decrement (gem defeated, HEAD B > 0); 34 relic_boss.c ge_frame() 5955 block
    hit; 19 rm_claw.c 6272 (Claw hitbox A > 0 when the engine is wrecked); 33 chase.c 5453-5460 respawn branch; 38 aqua.c
    dig_events() (dug spot, AT1 and RM1); 18 switches.c OT_DOOR (level_def_t.door_skip_ach = 18 on DV2: the 5098 E counter
    and the 5099 test, SW.skipE / skip_once).
  - 23-25 / 43-45 results.c `ach_results()` at ScoreValues D = 149 (once): `cards_commit()` first, then per stage index
    (DV 1: cards 1-10, 23, 43, 300000 ms; RM 2: 11-20, 24, 44, 480000; AT 0: 21-30, 25, 45, 300000) on
    `R.time_frames * 1000 / FPS`. Any mode, any character.
- **Pop-up (frame 8 -> 9):** results.c stage_end(): `ach_pending()` -> `fe_enter_ach_popup(ta, next)` (frontend.c): the
  game is unloaded, the front end and the gallery screen bank (scr_load(2)) are loaded, state FE_ACH (fe_trace_info index 9).
  fe_gallery.inc `gal_bind()` (scene objects + text, shared with gal_enter) and `pop_begin / pop_step / pop_draw` (shared
  with the gallery's Card Collector entry): 3 per page, sound 100, each page's entries written 2 when shown, "CONTINUE"
  waits for A / Start (presses in the first 30 frames ignored), one save_store() after the last page. Then: the route's next
  stage (`fe_unload(); game_next_stage()`), the Time Attack stage select, or the title menu, as stage_end() would have gone.
  Black background, no music. If the gallery data cannot load, the stage flow goes on without it (the gallery announces
  pending ones later). The gallery's own pop-up now waits for confirm too (the 150-frame auto-advance is gone).
- **Host trace:** `ach earn k`, `ach finish M A slash cube beam`, `ach results stage s ms t`, `gal popup enter n next x`,
  `gal toast k` / `gal toast done`. tests/test_achievements.py; test_gameplay dv4_end_results / dv4_exit and test_aqua
  snake_defeat press A on the pop-up (their runs earn a speedrunner achievement).

## Session 16: renderer, importer, VMU
- **render_pvr.c:** OFF_X 0.25, OFF_Y 60.25 (letterbox) / 0.25; letterbox bars at fixed 60 px. r_rect: alpha 0 draws
  nothing, alpha < 255 goes to the translucent list (dcmd kind 3) and keeps its alpha there.
- **tools/import_frame.py:** backdrop pieces (drawn and collision) at the instance x, y (hx = hy = 0).
- **objects.c:** obj_launch_crystal / obj_throw_petal / throw_items (and Serpentine petals) set layer 2.
- **relic_gima.c relic_gima_draw_overlay():** 8 rings × 4 rects, ring k alpha = blight_alpha × (1 − k/8), 85 × 48 px.
- **vmu.c vmuscr_char(character):** frame = ncrystal + {Lilac 0, Carol 1, Milla 2} × 4 + (tick / 360) % 4; called from
  game_update in MODE_PLAY and on the file select (fe_hub.c hub_vmu_file_char). vmu_lcd.bin: 6 crystal + 12 portraits.

## Session 17: Fortune Night flow (port: src/game/levels.c fn1-fn6, fn_flow.c, game.c, results.c; tests/test_fn_flow.py)
- Rows: stage 3, frames 29-34, music m_stage3 / m_stage3b / m_boss (slots 4 / 6 / 7 per act: level_music_slot), cards 31 /
  33 / 36 / 37 / 39 (FN6 none), checkpoints FN1 7700 / FN2 6520 / FN3 9536 / FN4 7670 / FN5 8430 (g4 2) + 10880 (g4 3),
  camera E = sign.x + 213 (12205, 15133, 12205, 15501), FN4 / FN5 exit X > W + 20, FN3 scripted exit, FN6 fixed box C 224
  D 0 E 960 F 311 (view (214, 71), RV) and walk-in X >= 400 without B, FN6 death / restart -> FN5 with g4 2.
- FN3 exit (fn_flow.c): host-measured with the RV placement (15060, 190): Serpentine at 14560, brakes to 14910 (RV 14910),
  taunts ~100 frames, passes, fade, FN4 282 frames after the trigger (RV 283); player walks to 15117 (RV). Sounds 17 + 134,
  61, 6, 61. HUD up 2 px a frame (7712), timer and pause off, Carol off the bike (7711).
- FN3 start: Lilac loses the samurai set (boss types 840-843) and the 8 MonitorBombs; Carol / Milla lose the Serpentine
  parts (821-825) and get the limit markers at 10831 / 11441 / bottom 1424.
- Hooks for the boss module: level_lock_markers / level_release_camera, level_music_slot, player_bike_off(p, park),
  results_begin_boss (FN6 row: group voice 8545, wipe 8546-8549), G.cam_nD / cam_nF, par_set_drift, fn_flow_panther_view.
- Host: row fields `g4` (act-start mode) and `tm` (stage timer); script `<f> RESBOSS` (results_begin_boss now).

## Session 17: Fortune Night bosses (port: src/game/fn_boss.c; tests/test_fn_bosses.py)
- **Types 800-859** (fp.h): UFO set 800-805, bulldozer 810-818, Serpentine 820-829, samurai 840-845; reused: OT_FN_BFLOOR /
  LAMP / COLUMN / RAMP (fn_gima.c), OT_WR_BARRIER, OT_FN_LASER / OT_FN_STURRET (fn_enemy.c), OT_FLAME (relic.c),
  OT_BOMBX (aqua_blast). Importer: FN_BOSS block (FNBOSS_TYPES_ID only in json 30-32 / 34, FNBOSS_SUB, rows fb_* / fs_*).
- **Hooks (one line each):** objects.c fn_boss_spawn / fn_boss_pre (test_collisions handlers, before combat) / per-object
  dispatch / draw (UFO rig order: lights behind the hull while sin B < 0, y-scaled beam, additive glow); game.c
  fn_boss_frame after fn_gimb_frame and fn_boss_input (CTRL.M during the escape); combat.c fn_boss_q20.
- **Level data:** level_def_t.fnb (fnboss_t): FN3 chase window / zones / cut A / Floor2 MV.B, samurai lock / music / CAM.D /
  g4 limit / wreck y / turrets (FN3, FN5). Flow interfaces used: level_lock_markers, level_release_camera,
  level_music_slot(4 / 6), music_fade, CK_FN3 / CM_FN3 (class sets and markers by character).
- **Sounds:** SFX_FN += 62 70 75 165 166 168 169 191-200 317 (11025 Hz; convert_sfx.sh).
- **Host:** BOSSHP sets the bulldozer, the Serpentine hitbox and the samurai (not the UFO); the `ob` line adds a5-a7, c, hd,
  lay, ang; FP_DBG_FNB prints the boss camera bounds per frame.
## Session 17: the Panther (port: src/game/fn_panther.c; tests/test_fn_panther.py)
- **Types 860-899** (fp.h OT_FNP_*): values, 13 rig parts (head .. foot FR) + ribs / tail, hitbox (Q20 via
  fn_panther_q20), laser, target, glow, wheel, scrolling floor (OD_SOLID|OD_BOXCOL), speed back, plane platform
  (OD_OBSTACLE), the assist set, ships / cables (FN5), runtime plasma / missile / rapid bullet / warning / speed line /
  shield / insta-shield. Importer: FN_PANTHER block (FNP_TYPES_ID, rows fp_*, FN6-only rows tied to 'scrollingfloor').
- **Hooks (one line each):** objects.c fn_panther_spawn / fn_panther_draw(pass); game.c fn_panther_input (the floor's
  CTRL.D auto-run, the FN5 pose lock) in the scripted-input chain, fn_panther_frame after fn_flow_frame,
  fn_panther_draw(3) (the white fade) after the front passes; combat.c fn_panther_q20; plat_host.c BOSSHP += OT_FNP_HIT.
- **Renderer:** r_set_xscale / r_get_xscale (an x scale for rotated sprites; render_soft.c and render_pvr.c, kept in the
  PVR translucent queue) for the laser's set_x_scale(16).
- **Flow interfaces used:** fn_flow_panther_view (8370), level_lock_markers / level_release_camera, music_fade,
  level_music_slot(4), player_bike_off, par_set_drift (8762), results_begin_boss(kill time) (8785), G.timer_stop,
  G.cam_nF, G.input_lock.
- **Per frame (SH4):** the rig is computed once (no per-part object scans; parts cached at spawn); runtime objects in one
  pass over the object list. FN6: fn6_ens 1024x512 + 256x256, 109 placed objects; fn5_ens 688 frames (27 panther rows) with a 1024 second atlas.
- **Sounds:** SFX_FN6 = 134 99 126 137 165 166 169 191 193 194 195 200 219 254 256 257 258 259 261 436 (228 KB at
  11025 Hz); SFX_FN45 += 126 193 (242 KB); convert_sfx.sh converts 137 254 256-259 261 436 at half rate.
- **Host:** FP_DBG_FP=1 prints `fp` (state), `fpdmg` (damage source / amount), `fpev` (rapid fall / missile blast);
  FP_DBG_FP=2 adds `fpp` (every part's x, y, angle).

## Session 17: Fortune Night lead notes (sounds, parallax, performance)
- Stage sounds: `level_def_t.sfx` is per act in FN (`FN(d, f, n, mus)` pastes `SFX_FN##n`; SFX_FN4 / SFX_FN5 are both
  SFX_FN45). snd_stage_load keeps ids shared with the next act and the character group, frees the rest, and logs
  `stage sounds N, sound RAM largest free block X KB` (game.c).
- parallax.c: `PMAXCH 32` chunks per parallax image (FN2's 5_1802 needs 30; over the limit par_load fails and the act
  draws no parallax).
- aqua.c bombs_frame: the enemy scan that arms a bomb monitor (3245 / 4056) tests distance before enemy_type and the
  image box; same result (a frame is at most 1024 px), much cheaper with FN's 600-870 objects.
- The AT / RM family passes (aqua_update, relic_frame, relic_gima_post) still run in FN because FN places RM-range
  reusable types (camera limits 368-371, chairs 336, layer switches 283, ...); about 2 ms per frame on the host profile.
  A per-type object index would remove that cost (not done: passes also act on runtime-created objects).

## Session 18: Fortune Night playtest fixes (port: tools/import_frame.py, fn_boss.c, fn_gimb.c, fn_gima.c)
- **Importer:** FNA_TYPES_ID maps BreakableFloor1/2 **1996 / 1997** (FN5) to OT_FN_BFLOOR (sub 1 = Floor2); their ens rows use
  the handle substitution (fnbfloor0 / 1 from the placed handle's images); piece rows: 1764 limited to json 30-32 / 35, 1995
  (FN5) added for json 34, both named fnbfpiece. fn5 exports 886 objects (was 830). **The lead must rebuild converted2/fn5**
  (`rm converted2/fn5; tools/build_level.sh 34 fn5 converted2`); other levels are unchanged.
- **fn_boss.c ufo_a1:** flag 5 (F_ARM) is set while G = 0 and no longer cleared (7367 only).
- **fn_gimb.c disco:** S.idle_t is the sash idle clock; dance when DiscoLight A >= 64, still (ground, B 0, M 0, no keys), not
  on the bike and ((idle_t - 1) * 14 / 100) % 12 > 10.
- **fn_gima.c arches:** 7225 `A >= 175`, 7226 `A <= 5`; the per-arch proximity test is the base's 256 x 128 box + 48 px (the
  old 200-px radius around the base's top-left corner dropped the ring's right half: the base went non-solid under her).
- Host tests: test_fn_bosses (ufo armour in every state, clangs without damage, floor cut and fly-off despite hits for the
  three characters, reset after a death, dormant after the checkpoint restart; FN5 56 floors, landing on them, rows break,
  entry -> defeat -> out of the arena -> FN6 for the three characters), test_fn_gimmicks_a (RV arch_lf catch and ride, RV
  arch_l / arch_r no catch, every arch of FN1-FN3 for the three characters + the bike, jump-off, far-side catch),
  test_fn_gimmicks_b (RV dance timing for the three, cancel / restart, walk-in, no dance on the bike).

## Session 18: pellet sounds, shield sprites, gallery voices (port: objects.c, player.c, shields.c, game.c, fe_gallery.inc)
- **Sounds on a pellet (RE col_4_20 / col_7_20 / col_20_20, RV .snd):** shot 28; pellet death (hit or ground) 17 +
  explosion02 (objects.c explode2, was 80 = Lilac's hurt voice); a hit adds 2414's 25 + the character's hurt voice
  (Lilac lilac_hurt<n> = Assets 80-83, Carol 114 / 115 / 118 / 119, Milla 179-182; die 128 / 178). player_damage no longer
  plays 17 (2414 plays 25 + voice only). Host check: `FP_TRACE_SID=1` prints `sid <id>` and now also `voice <name>` for the
  resident voices (game.c voice_play). tests/test_gameplay.py `pellet_hit_sounds_*` / `pellet_miss_sounds_*`.
- **Shield sprites:** G.shl = `<dir>/<dir>_shl` (tools/import_frame.py, acts without an on-map shield monitor: DV1 DV4 AT3 RM1
  RM3 RM5 FN3 FN6), loaded by shields_bank_load() after level_start when G.p.shield != 0, or on the first frame a shield
  exists without sprites (shl_need, once per act), freed in level_free. VRAM 256 KB while loaded. Host:
  `FP_SH_DBG=1` prints `shv <frame> <dir> <OTHERS.B> <sprite found> <_shl loaded>` (tests/test_levels.py `shield_*`); the log
  line `shields: no sprites for <dir> (<dir>/<dir>_shl missing)` means the act's data predates session 18.
- **Gallery sound cards:** gal/<name>.wav (11025 Hz sample, <= 65534 samples) or gal/<name>.adp (longer: streamed once on
  the music channel by music_play_once, idle when music_active() drops; Y.play_t = -1 marks it). Only sx_08b02 (card 70).

## Session 18: the Panther fight fixes (port: src/game/fn_panther.c; tests/test_fn_panther.py s18_*)
- **Hook order:** fn_panther_input (before player_update) now also runs the FN6 floor events that precede the movement:
  8508 / 8509 (belt latch), 8512 (left edge), 8515-8517 (belt speed, virtual right press); floor_mode (after the movement)
  keeps 8513 / 8514 / 8518 (belt) / 8519 / 8523 and 8550's walk-in X >= 400.
- **rig():** mode-2 branch (FP.g_m2 in act 6): pose_m2 (8613), the 8614 angles, head y - 0.5 I, hip rumble, tail, unrotated
  cannon / jaw, the wheel object placed at the hip (drawn by fn_panther_draw pass 0 at its own x / y / angle). body_moves:
  8620 / 8621 follow the player. No rig after 8762 (fight group off).
- **runtime_pass:** 8657 target each frame, plasma re-aim / spin / player burst / Milla target burst / 300-frame cap,
  missile birth-frame turn, blast_step (Explosion06 pattern, x - 10), warnings by age (31 frames). debris_fx = OT_DEBRIS
  (bosses.c motion). post_boss: 8759, 8769 wreck.
- **Shared files:** fp.h + game.c: `fn_panther_hud_dy()` added to hud_dy (FN6 8764: -80 from the closing pose); game.c
  host-only `FP_DIFF` (global 12 for tests).
- **Host:** FP_DBG_FP=3 prints `fpo frame slot born type x y st tm hidden` for every runtime panther object; the `fpp` row
  (FP_DBG_FP=2) ends with `w:x,y,shown,scale,angle` (the wheel).
- **Per frame (SH4):** unchanged structure (one pass over the object list for runtime objects); a missile blast now adds up
  to 45 short Explosion06 effects (as the original; at most ~4 blasts overlap).
## Session 18: the partner assist and the plane (port: src/game/fn_assist.c; tests/test_fn_assist.py)
- **Hooks (fn_panther.c, one line each):** fn_assist_spawn() at the end of the act-6 spawn; fn_assist_plane_mode(p) in place of
  the former plane_mode; fn_assist_frame(p, &ctx) after runtime_pass while 0x227c8 is on (ctx: hitbox, fight / mode-2 flags,
  cannon D, head A, laser segment); fn_assist_end() at the white flash (8762); HD('D') = fn_assist_closing(D) at C 2;
  fn_assist_cpu_finish(hit) for achievement 21; fn_assist_draw(0 / 1) from fn_panther_draw. game.c: the player is not drawn
  while fn_assist_cast_on() (the closing scene's cast stands in).
- **combat.c:** combat_shield_at(x, y, dir, A, force, vis) creates a ShieldInstaShield with fl 0x10 (alterable flag 4: 2740 /
  2741 leave the player's K at 0); combat_shield_overlaps(s, e). Shields keep every shared rule (2022's facing flip, the
  cyclone's "none exists" test) as in the original.
- **Objects:** the plane (OT_FNP_ASSIST) and Milla (OT_FNP_ADEF) are ordinary layer-1 objects (generic draw); the helper
  (OT_FNP_AOFF) and the projectile (OT_FNP_APROJ) have no an.b: fn_assist.c animates them (Lilac's forced speed 4|D| + 30,
  Active::force_animation keeps the counter) and draws them (pass 1 / pass 0). The box is a real OT_MON_RINGS (held: av[4];
  thrown: av[0] 2, Y 4: monitors.c's flight and objects.c's break, 6 petals). Dust puffs: OT_EXPLODE 'fndust', moved here.
- **Data:** FN6 ens rows fp_adef1 / 7 / 10 / 11, fp_aoff8 / 8b, fp_aproj / 7 / 8 and the cast fc_l* / fc_c* / fc_m* / fc_t*
  (tools/import_frame.py FNP_CAST, slices for the 40 poses): fn6_ens is now one 1024x1024 atlas (402 frames; was 1024x512 +
  256x256: about +470 KB VRAM). Sounds: SFX_FN6 += 241 (half rate, 2 KB) and 288 (Lilac's jump, 22 kHz, trimmed with the FN
  TRIM rule: 27 -> 12 KB): the FN6 stage group 176 -> 190 KB.
- **Host:** FP_DBG_FP=1 adds `fpa` (plane / Milla / helper state per frame), `fpas` (drop, arrive, jump, strike, land,
  knocked `by <object type>`, -1 the laser) and `fpcast` (the closing scene's D and cast x / sprite).

## Session 18: lead notes (performance, ceiling springs)
- **Per-frame cost cuts (same results; trace comparers and every suite unchanged):**
  - The AT / RM family passes skip a pass whose object types are all absent (`objects_alive_type`, a superset of the live
    types rebuilt on allocation): aqua_update's bursts, jelly, bricks, fireflies, slimes, whisps, jets and dig spots;
    relic_gima_post's paths, carry, wheels, wall plates, spinball, panels, flip panels, fireballs, axles, clocks, basement
    light and floors; rmboss_frame's shot / shock lookups. FN placed RM-range reusable types, so these passes ran every frame.
  - bombs_frame lists the enemies once per frame for bomb arming; enemies_absorb_crystals lists the absorbing enemies once
    per call (was a full scan per crystal in view).
  - The sensor snapshot (objects.c) has an order-preserving x-bucket index (512 px buckets); a query inside one bucket
    walks only that list.
  - bank_anim caches name -> index (keyed by bank and name text, hit re-checked against the bank, misses only for the same
    bank load).
  - relic_gima hangers: the handle list is built once per frame; fn_gima dragons: masks sorted by x for the 39 px test.
- **Flycast effect (update time per frame, holding right):** FN1 Lilac 13.6 -> 7.5 ms, FN2 Carol 6.8 -> 5.1, FN3 Carol
  13.9 -> 9.8, FN3 Milla 14.3 -> 10.1, FN4 Milla 11.9 -> 8.6; FN5 samurai arena (Carol) 10.7, FN6 2.6-3.7.
- **OT_SPRING_D = 29** (YellowSpringDown_416, FN2 / FN4 ceilings, RE event 3008 = event 3000 with G = 11): dispatched to
  aqua_spring (box contact from below while G <= 0, launch G 11 + gravity, sound 5). Types 20-27 are the parallax piece
  range and must not be reused.

## Session 19: DV3 spike ball, RM Milla background (port: src/game/gimmicks.c, tools/import_frame.py, assets.c, game.c)
- **Bamboo (gimmicks.c):** dv3_switch_spawn -> bamboo_link: per-type index lists of OT_BAM1..4 (creation order) and the
  5319 start phases (pivot tm = fmod(10 (N - 1 - j), 50)). bamboo_update per frame: joints, poles (position, then angle),
  pivot angles, heads, the 5228 contact test (bam_sensor_hit: the character sensor turned by direction (int)(N / 11.25)),
  then the 5229-5234 state machine. Work is proportional to the bamboo count (no per-object scans). RE_NOTES has the RV data.
- **Tiled pieces (level format, compatible):** a piece record with pad bit 2 is followed by a size record (s32 w, h at x, y;
  pad 0x81). level_load folds the pair into one piece_t with tw / th = the tile and w / h = the box; draw_pieces draws only
  the visible tiles, cutting the last row / column. Levels without Quick Backdrops are byte-identical in layout (only
  dv3, rm2, rm3, rm5, fn2, fn5 gain tiled pieces; fn4 rebuilds identically). Collision is rasterised from the tiles by the
  importer, so physics is unchanged in format. piece_t grew to 24 bytes (tw, th).
- **Synthetic piece images:** a solid Quick Backdrop tiles a 64 x 64 colour block (ids >= QB_BASE). In a split-colour level
  such blocks go to the colour set with the least image area (RM3: the second set; atlas sizes unchanged).
- **Rebuild:** dv3 (23), rm2 (26), rm3 (27), rm5 (29), fn2 (31), fn4 (33), fn5 (34) with tools/build_level.sh. RM3 atlases stay
  [(1024,1024), (1024,512), (1024,512), (1024,128)]; piece records 2702 -> 2782 (79 tiled pairs; the first tiles replace the single placements, + 1 solid box).
- Flycast to look at: RM3 cart route with Milla (cave wall, supports to the bottom, no parallax crystals in the dark band),
  RM3 / FN5 performance (draw time of the tiled layer-1 wall), DV3 bamboo swing and its hurt, DV3 rock wall near x 1200, y 1100.
## Session 19: Sky Battalion flow (port: src/game/sb_flow.c, levels.c, game.c, parallax.c, render_pvr.c / render_soft.c, tools/import_frame.py)
- **sb_flow.c** (stage index 4; attached by level_cur()->stage, never a level name): sb_flow_spawn after fn_flow_spawn in
  level_start, sb_flow_frame after fn_flow_frame, sb_flow_draw(0) at the end of objects_draw, hud_dy += sb_flow_hud_dy().
  SB1: pads (904 + masks 905), launch (G.clear_t + G.exit_to), done ships, airships (909, drawn at 0.25), night overlay
  (911) and the Kujacker hand-off; SB2-4: pads, the opening (913 planks, timer / HUD / g4), boss camera locks on the
  CameraLimitBoss markers while OT_WR_RAIL / OT_WR_BARRIER exist (level_lock_markers / level_release_camera).
- **Hooks (fp.h):** sb_flow_ship_done(flag), sb_flow_night(), sb_flow_intro(), sb_flow_pad_down(i), sb_flow_act(),
  sb_flow_hud_dy(). Host trace lines "sbflow ..." under FP_TRACE_SND.
- **level_def_t.clear_free:** the act-end fade leaves the player alone (SB1 launch: no 3311 walk, no pin, control kept).
- **levels.c:** rows sb1-sb4 (stage 4, frames 35-38, SFX_SB1..4, cards 41 / 44 / 48, checkpoints, exit EX_SBSHIP -> sb1,
  entries EN_SB1 / EN_SBSHIP, music slots); level_route_next: stage 3 -> level_first_of_stage(4) (Classic FN6 -> SB1).
- **Parallax file v2** (written only when an image has flags; other levels stay v1): image header 4 x u16 with flags
  1 = ARGB4444 chunks (`<name>_pa<i>_<k>.tex`, FPTX fmt 4), 2 = half resolution (drawn x2), 2048 = SB bob (9198),
  4096 = world-positioned image (D = world x; the zaoship). Renderers accept texture fmt 4 (PVR ARGB4444 non-twiddled;
  soft: per-texel alpha).
- **Importer:** SB_TYPES / SB_ENS (frames 36-39 only), BAKE_SOLID (ShipPlatform1/2/3 rasterised into collision, then
  dropped), tall pieces first in the atlas order (SB1's 996 x 708 ship).
- Flycast to look at: SB1 pads and the launch fade, the SB1 flying entry, the ship opening planks, the ARGB4444 mist on
  PVR (VRAM free per act), the night overlay, sound RAM per act, the parallax bob.

## Session 19: Sky Battalion bosses (port: src/game/sb_boss.c, sb_kujacker.c, sb_chopper.c, sb_spade.c, sb_satellite.c, sb_grappler.c)
- **sb_boss.c** (both Makefiles): sb_boss_spawn builds per-type instance lists (OT_SBB_FIRST..LAST = 1150-1279, placement
  order) and calls each encounter's spawn; sb_boss_pre / sb_boss_frame / sb_boss_draw_obj dispatch to sbs_* (SB3), sbk_* (SB1),
  sbc_* (SB2 chopper), sbp_* (SB2 Spade), sbg_* (SB4); each returns at once when its boss is not placed. Shared helpers in
  sb_boss.h (sbb_get / sbb_count / sbb_spawn, sbb_hurt / sbb_vulnerable / sbb_csens_hit, sbb_fight_start, sbb_arena_end,
  sbb_lock_edge, sbb_draw / sbb_draw_yscaled); the exit plane and the compass; sb_boss_q20 (combat.c); the plane's I = sb_gim_cannon_kills()
  (sb_gim.c, 9319) + sb_boss_cannon_down() calls.
- **Hooks used:** sb_flow_night() (SB1 start), sb_flow_ship_done(flag) via sbb_arena_end, results_begin_boss() (SB1 end),
  level_music_slot(4) via sbb_fight_start, the flow's camera locks (level_cam_locked).
- **objects.c:** anim_tick speed for OT_SB_SPADE (force_speed in o->c). **fp.h:** OT_SB_* (Kujacker 1170-1188, chopper
  1200-1206, Spade 1220-1224, satellite / grappler / plane in the block). **levels.c:** SFX_SB1 + 99, SFX_SB2 + 71 / 155 / 240 / 445.
- **Importer (SB_BOSS block):** SBBOSS_TYPES_ID, SBBOSS_ENS rows sbs_* / sbg_* / sbc_* / sbp_* / sbk_*, SBBOSS_ENS_WITH (effects
  kept in the boss levels' _ens banks).
- **Host debug:** FP_DBG_SBS / SBG / SBC / SBP / SBK print the state per frame; FP_SB_ASSIST_I = frame to count two cannons down.
- **Sounds (tools/audio/convert_sfx.sh, 11025 Hz, trimmed):** + 71 155 445 (Spade). Groups: SFX_SB1 91 KB, SFX_SB2 126 KB.
- Flycast to look at: the Kujacker (many parts and up to ~220 lightning spokes on screen during the charge), sb2's _ens texture
  (about 1 MB: Spade + chopper), the satellite beams' y-scaling on PVR, sound RAM per act.
## Session 19: Sky Battalion enemies and gimmicks (port: src/game/sb_enemy.c, sb_gim.c, objdefs.c, tools/import_frame.py)
- Types: fp.h SB_ENEMIES 1024-1099 (1024 hopper ... 1038 magnetizer line, runtime 1060-1065), SB_GIM 1100-1149 (1103 the
  breakable floors, 1117 / 1118 / 1119 platforms, runtime 1140-1142). Importer blocks SB_ENEMIES / SB_GIM (SBG_SUB member
  index | layer << 8 in the lvobj anim field).
- Passes: objects.c spawn (sb_gim_spawn then sb_enemy_spawn), objects_platforms sb_gim_pre (one-way flags), per-object
  sb_enemy_obj / sb_gim_obj, game.c sb_gim_frame (paths, carry, opening hold / release, families). Q20 via combat.c
  enemy_type_list (sb_enemy_q20 / sb_gim_q20: the cannon base). Per-type instance lists; nothing runs without the types.
- Hooks used: sb_flow_intro() (platform hold / release). Exported: sb_gim_cannon_kills() (PlayerAssist.I for the boss agent).
- Sounds: SFX_SB2 + 312 263, SFX_SB3 + 312, SFX_SB4 + 312 (263 new, 11025 Hz trimmed); groups 81 / 130 / 154 / 124 KB.
- Tests: tests/test_sb_enemies.py (RV cycles), tests/test_sb_gimmicks.py (opening ride, data rows, hazards).

## Session 19: Sky Battalion object gaps (port: src/game/sb_enemy.c, sb_gim.c, gimmicks.c, aqua_gim.c, levels.c, player.c, sb_flow.c, objects.c, tools/import_frame.py)
- **Types:** OT_SB_WARN 1039 (SB1 / SB3 placed EffectWarning_1528; the importer remaps frames 36 / 38, SB4's stays the boss
  module's 1188), runtime OT_SB_SHIPSHOT 1066, OT_SB_HUBBALL 1067; OT_SB_DOOR 1133 (SwitchDoor_2102, was DV's 96).
- **sb_enemy.c:** hub_spawn / hub_frame (SB1 deck fire, from sb_enemy_pre; per-type lists, the q8 obstacles collected once at
  spawn), active_at() (Chowdren's INACTIVE rule), dna_warnings() (SB3), sb_enemy_draw(0) draws the ship shots on the 0.25 layer
  (objects.c after sb_flow_draw).
- **sb_gim.c:** sb_gim_door_rise(c) (called by sb_flow.c pads() after a sink step below 24), the spin-cup grab rule (grab_ok,
  sound 7, MV.S 0 via c_nospd until airborne after landing). player.c roll_move honours c_nospd for every character.
- **sb_gim.c chains():** the 10001 crush on a rising chain wall now = BottomBig on the mask + ceiling on TopBig (RE).
- **gimmicks.c:** pb_is / pblock_box include OT_SB_PBLOCK4 (64 x 64 box); q27_type includes OT_SB_BTUNNEL (objdefs: solid +
  hidden). **aqua_gim.c:** SB4's spawner 2 = 8 links, start angle 0. **levels.c:** PADS_SB2 / SB3 / SB4.
- **Importer:** ROW_FRAMES ('sbwarn' SB3 only; SB1 uses the boss block's 'sbk_warn'), the mt_warn / fp_warn rows kept to their
  old frames (they no longer duplicate into SB1 / SB4), DV's 'warning' row skipped in SB, ROW_HANDLE_SUB (SB4 swing parts take
  2319-2321's images), 'sbshot' / 'sbc_ball' / explosion06 exported with the SB1 airships.
- **Rebuild:** sb1-sb4 (json 36-39). Flycast to look at: SB1 deck fire (warnings, the ball, the blast near the player, the
  ship shots over the airships), SB3 DNA warnings at the edges, SB2 / SB4 doors, SB3 cup -> tunnel ride, SB4 swings' images.

## Session 20: bug-fix addendum (port: fn_assist.c, fn_panther.c, game.c, objects.c, plat_*.c, sb_kujacker.c, sb_gim.c, sb_flow.c, objdefs.c, switches.c, relic_gimb.c, tools/import_frame.py)
- **fn_assist.c:** fn_assist_reset() (called by fn_panther_spawn on every level load, before its FN5 / FN6 test);
  fn_assist_closing() puts the hidden player at Y 260 + ORIG_REF_DY, stopped (8764).
- **fn_panther.c post_boss():** 8749 / 8750 every frame of the post-boss group.
- **game.c:** game_player_visible() (game_draw and FP_TRACE_PDRAW); music_fade() remembers music_serial() and a new track
  cancels the fade (music_serial() in plat_dc.c / plat_host.c); game_draw order: pieces layer 0, layer-1 pieces with the 0.8
  scroll (pad bit 1), objects_draw(-1) + sb_flow_draw(-1) (json layer 1), the other layer-1 pieces, objects_draw(0) ...
- **objects.c:** pass -1 = objects with layer -1 (json layer 1 of a mapped frame), drawn at 0.8; pass 0 skips them.
- **sb_gim.c:** lvobj anim 0x8000 -> layer -1; cannons(): a beaten base flies off (kx 5, ky + 0.25, angle) until D 31.
- **sb_flow.c:** sb_flow_draw(-1) draws the airships / planks; pass 0 keeps the night sky.
- **sb_kujacker.c:** K.td[16] (target directions), tdir(); poses() picks sbk_tail / _d1 / _d2 / _d3 / _d31 per feather;
  FP_DBG_SBK prints td.
- **objdefs.c:** OT_HSENS / OT_HSENS2 / OT_HISWITCH / OT_HIMOVER get OD_HIDDEN.
- **switches.c:** the rule-2 start press runs after the plate reset.
- **relic_gimb.c:** cart_frame() bumper loop (front / rear, by direction), S.bump[2] for plates().
- **Importer:** sbk_tail_d1 / d2 / d3 / d31 rows; write_lv2(..., mapped) sets 0x8000 on SB_GIM objects of json layer 1.
- **Host:** FP_TRACE_PDRAW (pdraw f visible lv mode); BOSSHP also sets sb_gim_q20 types.
- **Tests:** tests/test_transitions.py (new), tests/test_rm_visuals.py (new); s20 checks in test_fn_assist, test_sb_bosses
  (boss music kept, tail glow), test_sb_gimmicks (ship exits), test_rm_gimmicks_b (cart reversal), test_sb_flow (night music).

- Session 20 addendum: the Kujacker tail glow is an overlay animator per feather (`K.tov[16]`, ticked with anim_tick(-1))
  drawn with frame_draw_rot at the tail angle; its images are the original glow frames with rows >= 48 blanked by the
  importer (TOP_ROWS) so they crop to the eye. r_sprite_rot (soft) and the PVR cull radius use the farthest image corner
  from the hotspot, so sprites whose hotspot lies outside the image rotate correctly.

## Session 21
- Jade Creek = stage index 5, set_frame 39-43 (json 40-44), port jc1-jc5; types 1280-1309 flow, 1310-1369 enemies, 1370-1429
  gimmicks, 1430-1479 bosses (acts 1-4), 1480-1535 act 5. Music: m_stage5 / m_stage5b, slot 4 m_midboss (JC5 m_boss).
- HUD offset: game.c hud_offset() (sum of the modules' shifts); host FP_TRACE_HUD prints it per frame.

## Session 21b
- Trap Hideout = stage index 8, set_frame 56-58 (json 57-59), port th1-th3; types 1536-1559 flow, 1560-1619 enemies (+
  enemy_kit.h), 1620-1679 traps, 1680-1791 bosses. Music m_stage7, slot 4 m_midboss / m_boss2 (TH3 g4 >= 5).

## Session 21c
- Thermal Base = stage index 6, set_frame 44-49 (json 45-50), port tb1-tb6; types 1792-1823 flow, 1824-1887 enemies, 1888-1967
  gimmicks, 1968-2047 bosses (tb_boss.c dispatch). Music m_stage6 (TB1-3) / m_stage6b (TB4-6), slot 4 m_midboss / m_boss2 (TB6).
  Frame order: tb_flow, enemies, tb_gim_frame, tb_boss_frame; tb_gim_draw(3) after hud_draw (key-card count).

## Session 21d
- jc_boss.c M_AMBUSH (JC5 start), tb_boss_shrimp.c camera release, objects.c `objects_plat_oneway` (th_gim.c pillars).

## Session 23
- Pangu Lagoon = stage index 7, set_frame 50-55 (json 51-56), port pl1-pl6; types 2048-2079 flow, 2080-2143 enemies, 2144-2223
  gimmicks, 2224-2303 bosses (MAX_OT 2304). `level_def_t.lilac_only`; `player_t.sboost`; game.c `act_fade_step()`.

## Session 24
- Modules: bg_flow.c, bg_enemy.c, bg_gim.c, bg_boss.c (+ bg_boss_mech.c Absolution, bg_boss_sparky.c Sparky + BG5 ride,
  bg_boss_dail.c Prince Dail); fd_flow.c, fd_enemy.c, fd_gim.c, fd_boss.c (dispatch) + fd_boss_r12.c (bosses A: push beam,
  morpher, mech Serpentine; RoboSquid = tb_boss_squid.c) + fd_boss_r34.c / _3b.c / _4b.c (bosses B: Kingdom Stone, mutated
  Milla, FD4b mech / suit / Brevon; bulldozer = fn_boss.c); ending.c, cutscene.c (+ cutscene_example.c).
- Hooks: bg_flow_opening_cleared (BG4 mech end -> g4 5 at (444, 1800), music), fd_gim_hangar_break (fd1a Sign.D 160),
  fd_gim_scroll_floor (FD1b treadmill during the morpher), fd_gim_siren_alarm / fd_gim_siren_state (fd2b), fd_enemy_pod_rain,
  fd_enemy_gatlings_wake (fd3a alarm), fdb34_* (fd_boss.c -> bosses B), results_begin_boss / results_request_end ->
  game_ending_begin (FD4b), player.c ST_RIDE chain (bg_boss_ride_m: BG5 Sparky ride; fd_gim_ride_m: transporter pipes).
- level_def_t: music_fade_g4 (BG4), exit_hold (fd4a: 100 frames before the fade), mus4b / mus4b_g4 (fd4b: m_boss5 -> m_boss7
  at g4 3), vox lists (bg2, bg6, fd1b). levels.c ROUTE covers stages 0-13 (FD rounds 10 -> 11 -> 12 -> 13 -> ending).
- obj_near_view uses the bank's BOX1 whole-image box (D-113). Path-platform registry 24 entries (was 12).
- Host: FP_BGB_BOT (boss bot), FP_F4B_* / FP_F3B_LIVES / FP_G4 (test knobs), FP_DBG_FDB / FP_DBG_TBQ, script RESBOSS /
  RESEND / ENDTRY / ENDSTATS.

## Session 26b (gimmick gaps: pads, fd2b vacuum, JC5 rain)
- Pads: levels.c PADS_TB4 / TH3 / BG2 / JC2 / JC3 / JC4 / FN4 / FN5 (RV dumps of the pad list), level_def_t.pads_inert (JC3);
  carol_bike.inc keeps the pads' slots in a list at pads_spawn (pads_events / pad_by_A no longer scan every object) and logs an
  on-map pad with no table row (host); obj_pop_crystal (objects.c, ITEMF_PIN) is the pads' crystal (PinballMovement 36 / 25).
- fd2b: level_def_t.air_vacuum -> water.c air_vacuum_events (in water_events' slot, after shields_update) and water_draw_hud's
  GUIAir / countdown digits without water; the siren state read is the previous frame's (fd_gim_siren_state, latched).
  Patches only on Easy / Casual (fe_difficulty = global 12 >= 2). SFX_FD2B + 20, 91 (11025 Hz, trimmed).
- JC5: jc_boss5.c rain_frame (before storm()) with a fixed 50-drop / 64-splash table; one runtime object OT_J5_RAIN (1506, layer 4
  = drawn in objects_draw pass 2) draws them all; par_set_tint (parallax.c, the back pass and the water strip) for 11614's
  layer-0 colour; the rain loop 293 on a looping channel (snd_loop_orig at spawn, volume each frame, muted by the pause, stopped
  at the next level spawn). SFX_JC5 + 293 (11025 Hz, 12 s loop).
- Host: FP_DBG_AIR (vac lines), FP_DBG_J5 adds "j5rain n vis spl skyC vol tint ch"; tests/test_gimmicks_m.py.
- RV harness: GT (global 3) is not reset at fd2b's start (it carries the previous frame's count), so trace phases of GT % 60 /
  % 120 events there depend on what ran before; JC5 resets it. Global 12 can be set before a restart (scratch rt2.sh: set
  {double}(*(long*)0x285ec68 + 96)); set it back to 1 afterwards, the running game keeps it for the next trace.

## Session 25 (the story runtime, pellets, verification)
- src/game/story.c / story.h: `story_gate()` (S.run && fe_mode == 2 && !G.time_attack && G.file_mode == 0) is the only gate;
  `story_new_game` (hub NEW ADVENTURE / character select), `story_stage_end(stage)` (results / achievement pop-up only),
  `story_continue`, `story_epilogue()` (ending.c, Adventure only), `story_reset()` at every entry point that leaves a run.
  G.mode MODE_STORY while a scene plays; `chr_variant()` picks the variant of the same json frame and g4. story/story.txt rows:
  `<id> <room> <g4> <next> sid= [skip=] [stage=] [panel=] [trig=new|end:I|ending] [chr=]` (STORY_ROWS 96, STORY_MAX 64 sids).
- src/game/cutscene.c: the script player (actors with ink, camera, parallax visibility, fades, captions / subtitles, dialog
  box, two voice slots, rigs); per scene `game_sfx_set()` loads only the script's sound ids that are not resident.
- Save v5 (save.c): per file `story` (last completed sid + 1), global 8 bytes of seen flags (by sid); v3 / v4 load.
- obj_spawn_pellet(x, y, dir, layer): the creator's layer (D-120); obj_effect_l(): an effect on a given layer.
- Verification discs: demo.txt `<f> SCENE <id> <g1>` -> story_verify_play(); logs `scene ... vram ... snd` and, every 30
  frames in a scene, `story lv ... vram ... snd` (plat_dc.c).
