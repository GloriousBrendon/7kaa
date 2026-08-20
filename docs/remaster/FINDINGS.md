# 7KAA Remaster — Findings

Research findings for the 7 Kingdoms: Ancient Adversaries remaster effort, written for AI coding agents. Every claim below carries a citation in the form `` `<file> :: <Symbol>` `` — a real source path, followed by `::`, followed by a function, class member, or constant — which `scripts/check-citations.sh` verifies still exists at that path. Where a line number adds value it's given alongside the symbol, but the symbol is the durable part; line numbers drift, symbols mostly don't.

See `CLAUDE.md` (repo root) for the condensed, always-loaded version of the constraints this document backs up.

## Executive Summary

7KAA is a ~225K-line C++11 RTS from 1997, now a community-maintained open-source port (autotools, SDL2, OpenAL-soft, ENet, libcurl, gettext — v2.15.6 at time of writing). The SDL2/networking port is already done; what's *not* done is the source of the fan-reported headaches/nausea (no vsync, a busy-spin main loop, discrete jump-scroll camera), a dated AI (hardcoded difficulty tables plus hidden "cheat income" rather than smarter play, several self-acknowledged dead-code stubs, a non-admissible pathfinding heuristic), and a fixed 800×600 8-bit-palette internal render resolution. The single biggest structural risk for any change is the multiplayer **lockstep determinism** mechanism (`src/OMP_CRC.cpp`) — it, and the build flags that support it, must be understood before touching anything simulation-adjacent.

## Codebase Overview

- ~225,400 lines across `src/` (285 `.cpp` files) + `include/` (195 headers), plus vendored adapters under `src/curl/`, `src/enet/`, `src/imgfun/`, `src/openal/`.
- Build: GNU Autotools (`configure.ac`, `Makefile.am` per directory) — no CMake, no IDE project files. C++11, GCC 4.6+.
- Third-party: SDL2 (video/input), OpenAL-soft (audio), ENet (multiplayer UDP), libcurl (7kfans.com lobby, optional), gettext (`po/`, 6+ languages).
- Naming convention: legacy 8.3-style, "O"-prefixed filenames are literally "Object ___" — `src/OFIRM.cpp` = Object Firm, `src/OUNIT.cpp` = Object Unit, `src/OSYS.cpp` = Object System, `OAI_*.cpp` = AI logic per domain. One class family per subsystem file. This convention predates the project's move to git — see the `//### begin alex ###//`-style comments in the Appendix, which are pre-version-control developer diff markers still embedded in the code.
- `data/` (102M): all game assets in custom 1997 binary formats — `.SPR` (sprite sheets), `.ICN`/`.COL` (image + palette pairs), `.RES` (resource archives: fonts, cursors, wave audio), DBF-style `.dbf`-in-`ENCYC*/` (encyclopedia content). `tools/` holds dev-only offline converters (`dbfdump`, `delibdb`, `deresx`, `fontdump`, `icnpack`, `paldump`, etc.) for these formats.

## Architecture Findings

### Global singleton pattern

The entire game-state graph — roughly 90 objects — is declared as file-scope globals in `src/AM.cpp :: main()`'s translation unit (the globals themselves are declared just above `main()`, not inside it). There is no dependency injection; every subsystem reaches every other subsystem via `extern` globals declared in headers. Representative examples: `sys` (`Sys`), `vga`/`vga_front`/`vga_back` (`VgaBuf`), `world` (`World`), `power` (`Power`), `unit_array`/`firm_array`/`town_array`/`nation_array`/`bullet_array` (the per-object-type array managers), `game` (`Game`), `info` (`Info`), `battle` (`Battle`), `crc_store` (`CrcStore`).

This is the single biggest structural obstacle to modernization (testing, threading, multiple game instances) but also means most subsystems can be touched incrementally without a wholesale rewrite — the coupling is breadth-first (everyone touches globals) rather than depth-first (deep call chains), so isolated fixes (e.g. rendering) don't require touching the object-array managers.

### Main loop

Entry point: `src/AM.cpp :: main()`. Flow: config load → `Sys::init()` → dispatch into `Game::main_menu()` or multiplayer/test modes → `Sys::main_loop()`.

`src/OSYS.cpp :: Sys::main_loop()` is a `while(1)` loop that every iteration: calls `src/OSYS.cpp :: Sys::yield()` (event/audio/network polling — confirmed **no `SDL_Delay` call anywhere in the codebase**, repo-wide grep returns zero matches), then gates simulation advancement through `src/OSYS.cpp :: Sys::should_next_frame()`, and separately gates presentation through `Vga::flip()`'s own timer (see Rendering section). Per-frame simulation order, when a frame does advance, processes each object-type array in a fixed sequence (units → firms → towns → nations → bullets → world → weather/particle effects), and every `include/OSYS.h :: FRAMES_PER_DAY` (= 10) frames runs a "day tick" (`info.next_day()` and each array's `next_day()`).

### Memory & containers

Custom allocator: `class Mem` with `mem_add`/`mem_del`/`mem_resize` macros (`include/ALL.h`) wrapping `malloc`/`realloc`/`free`, with optional leak tracking disabled by default (`NO_MEM_CLASS`). Custom dynamic-array classes (`src/ODYNARR.cpp`, `src/ODYNARRB.cpp`) substitute for `std::vector` throughout the object-array managers. Raw pointers and `#pragma pack(1)` C structs are used directly for save-game and resource I/O — no RAII containers in the hot paths.

### The determinism / lockstep mechanism — read this before touching simulation code

Multiplayer is **lockstep**: every peer runs the same simulation from the same inputs and periodically compares state via CRC. `src/OMP_CRC.cpp` computes per-object CRCs (`SpriteCrc`, `BulletCrc`, `UnitCrc`) — for example unit position is synced via the `stop_x_loc`/`stop_y_loc` fields (`src/OMP_CRC.cpp`, `c->stop_x_loc = stop_x_loc;`). If a value isn't written into one of these CRC structs, it isn't part of the determinism contract.

The build enforces bit-reproducible floating point **only partially**, and this is a real, currently-unaddressed gap: `configure.ac` unconditionally applies `-fsigned-char` (line 51, and the build hard-errors via `AC_ERROR` at line 63 if the compiler won't cooperate — this one is a mandatory constraint). But `-mfpmath=387 -ffloat-store` (`configure.ac` lines 68-79) is applied **only if an `AC_COMPILE_IFELSE` probe for 387-FPU support passes** — on failure it just logs "no" and silently continues (no error). On non-x86 targets (ARM, WASM, Apple Silicon-native builds) this probe presumably fails or is inapplicable, meaning **those builds get no equivalent floating-point-determinism protection today**. Any remaster work targeting a new architecture must treat this as an open problem, not an assumption that "the build already handles it."

**"Is my change safe?" checklist** — before editing anything simulation-adjacent, ask:
- Does this value get written into a struct in `src/OMP_CRC.cpp`? If yes, changing how it's computed changes multiplayer sync.
- Does this affect the *order or content* of unit/nation/AI decisions (attack targets, build choices, pathfinding results)? Those decisions drive CRC-synced state even if the decision-making code itself isn't directly in `src/OMP_CRC.cpp`.
- Does this touch floating-point math, RNG seeding/consumption order, or struct layout used by saves (`src/OGFILE2.cpp`, `src/OGFILE3.cpp`) or network packets?
- If yes to any of the above: this needs a `ConfigAdv`-style toggle (see below) defaulting to legacy behavior, not a silent behavior change.

**Worked example — why camera position is safe to smooth but AI decisions are not:** `Matrix::top_x_loc`/`top_y_loc` (the map scroll position) is written to save games (`src/OGFILE2.cpp`, `zoom_matrix->top_x_loc = map_matrix->cur_x_loc;`) but is **not** present anywhere in `src/OMP_CRC.cpp`'s synced fields (only unit `stop_x_loc`/`stop_y_loc` — a different, unrelated per-unit field — appears there). So smoothing camera motion is a client-local presentation concern, safe to change without a determinism strategy. AI decision logic, by contrast, determines unit orders, which *do* feed CRC-synced state — any behavior change there needs the toggle pattern.

### Existing community precedent: the `ConfigAdv` toggle pattern

Bugfixes to gameplay-affecting behavior already ship behind named boolean flags in `include/ConfigAdv.h`/`src/ConfigAdv.cpp`, defaulting to legacy behavior for save/replay compatibility — e.g. `fix_path_blocked_by_team` (`include/ConfigAdv.h :: fix_path_blocked_by_team`, defaulted at `src/ConfigAdv.cpp` line 233, parsed at line 313). **This precedent should be followed by every future phase, not just AI work** — any change to simulation-adjacent behavior should ship as a new named `ConfigAdv` flag, not a silent replacement.

### Save-game format fragility

`class GameFile` (`src/OGFILE.cpp`, orchestration) plus `src/OGFILE2.cpp`/`src/OGFILE3.cpp` (the actual per-object-array serialization, ~1550 and ~2488 lines respectively) write raw `#pragma pack(1)` struct dumps. The only compatibility check is a coarse `class_size` field in `SaveGameHeader` — there is no real field-level versioning or migration. Any change to a serialized struct's layout breaks old saves unless deliberately handled. `src/OGF_V1.cpp`/`include/OGF_V1.h` exists as a legacy-version-1 compatibility shim, demonstrating the project has had to solve this problem before.

### Asset & resource formats

- `.RES` archives (`data/RESOURCE/`): `class Resource` (`include/ORES.h`, `src/ORES.cpp`) and `class ResourceDb` (`include/ORESDB.h`, `src/ORESDB.cpp`) — index + data buffer reader, used for fonts, cursors, wave audio.
- `.SPR` sprite sheets (`data/SPRITE/`): `class SpriteRes` (`include/OSPRTRES.h`, `src/OSPRTRES.cpp`), loaded per unit/building/effect family via `SpriteRes::load_sub_sprite_info`.
- `.ICN`/`.COL` pairs (`data/IMAGE/`): UI chrome and nation-specific art, image + companion palette.
- DBF-style database files: `class Database` (`include/ODB.h`, `src/ODB.cpp`), a dBASE III-style reader used for the `data/ENCYC*/` encyclopedia content. Offline tooling: `tools/dbfdump`, `tools/dbf.pm`.

### What's already modernized vs. not

**Already done** (community fork work, not 1997-original): full SDL2 video/input port (`src/OVGA.cpp`, header comment credits "Copyright 2010,2015 Jesse Allen" alongside the original Enlight Software copyright); ENet-based UDP networking (`src/enet/multiplayer.cpp`) replacing legacy DirectPlay; libcurl-based lobby integration (`src/curl/WebService.cpp`); OpenAL-soft audio; gettext i18n (`po/`, `src/LocaleRes.cpp`).

**Not yet modernized** (this is the remaster's actual scope): fixed 800×600 8-bit-palette internal render resolution (see Rendering section); 100%-CPU-side software blitting with only a final single SDL2 texture upload per frame — no shader pipeline, no GPU sprite batching; legacy binary asset formats unchanged since 1997; fragile save format (above); AI still difficulty-tables-plus-cheat-income rather than genuinely adaptive (see AI section); zero video/accessibility settings in the in-game Options menu (`src/OOPTMENU.cpp`) — confirmed by direct grep: none of `vga_full_screen`/`vga_keep_aspect_ratio`/`vga_window_width`/`vga_allow_highdpi` (the `ConfigAdv` video fields) appear anywhere in `src/OOPTMENU.cpp`; they're only reachable by hand-editing `config.txt` or the `-win` CLI flag (`src/CmdLine.cpp`).

## AI Subsystem Findings

~41 files, ~22,100 lines (~12-13% of the codebase), split across three layers:
- **Nation/kingdom strategy** (24 `src/OAI_*.cpp` files, ~12,400 lines): `OAI_MAIN` (entry point), `OAI_GRAN` (alliances/"grand plans"), `OAI_DIPL`/`OAI_TALK` (diplomacy propose/react), `OAI_ATTK` (attack assembly), `OAI_SEEK`/`OAI_ACT`/`OAI_ACT2` (expansion), `OAI_CAP2`/`OAI_CAPT` (town capture), `OAI_MARI`/`OAI_MAR2`/`OAI_MAR3` (sea), `OAI_SPY`, `OAI_BUIL`, `OAI_MILI`, `OAI_TOWN`, `OAI_MONS`, `OAI_ECO`, `OAI_TRAD`, `OAI_QUER`, `OAI_DEFE`, `OAI_INFO`.
- **Object-level AI hooks** (~9,700 lines): `src/OTOWNAI.cpp :: Town::process_ai()`, `src/OUNITAI.cpp :: Unit::process_ai()`, `src/OFIRMAI.cpp :: Firm::process_common_ai()`, plus 9 per-firm-type `OF_*2.cpp` and 5 per-unit-type `OU_*2.cpp`/`src/OSPY2.cpp` files.
- **Pathfinding** (~6,700 lines): `src/OSPATH.cpp` (core search), `src/OSPREUSE.cpp`/`include/OSPREUSE.h` (`SeekPathReuse` — path caching/reuse for grouped units), `src/OSPREOFF.cpp` (deferred/background search continuation), `src/OSPRESMO.cpp` (path smoothing).

### Think-cycle cadence

The game runs `include/OSYS.h :: FRAMES_PER_DAY` (= 10) frames per in-game day. AI does not think every frame. `src/OAI_MAIN.cpp :: Nation::process_ai_main()` round-robins through 12 top-level categories (`think_build_firm`, `think_trading`, `think_capture`, `think_explore`, `think_military`, `think_secret_attack`, `think_attack_monster`, `think_diplomacy`, `think_marine`, `think_grand_plan`, `think_reduce_expense`, `think_town` — all confirmed present in `src/OAI_MAIN.cpp`) using a modulo-day selector against an interval table:

```
static short intervalDaysArray[] = { 90, 30, 15, 15 };  // indexed by config.ai_aggressiveness
```
(`src/OAI_MAIN.cpp`, near the `process_ai_main` dispatch, confirmed by direct read.) **Higher difficulty makes the AI think up to 6× more often** (every 15 days vs. every 90), not smarter per-decision.

### Difficulty = hardcoded tables + cheat income, not smarter play

`src/OCONFIG.cpp :: Config::change_difficulty()` applies ~13 hardcoded parameter tables indexed by difficulty level (AI nation count, starting cash asymmetry favoring the AI at higher difficulty, `ai_aggressiveness`, fog-of-war, monster aggression, etc.).

`ai_aggressiveness` directly biases attack-target scoring toward the human player: `src/OAI_ATTK.cpp` adds a flat rating bonus (roughly +100 to +300 depending on difficulty) to attack-target scoring specifically `if (!nationPtr->is_ai())` — i.e. AI nations are coded to prefer attacking the human over other AI nations, scaled by difficulty.

**Cheat income:** `src/ONATIONB.cpp :: NationBase::add_cheat(float cheatAmt)` (declared `include/ONATIONB.h :: NationBase::add_cheat`) injects free cash directly into an AI nation's income ledger, obscured across a randomized income-type bucket specifically so it doesn't show as a distinct line item to the player. It's called from the low-cash "AI is broke" rescue path in the monster-attack AI, and separately from a tutorial-mode path that keeps the tutorial opponent from surrendering. This is the actual mechanism behind "AI gets smarter at higher difficulty" — it's mostly "AI gets subsidized," not "AI reasons better."

### Known dead code / self-acknowledged bugs

- `src/OAI_MILI.cpp :: Nation::think_close_camp()` is stubbed to `return 0` immediately — **the AI never voluntarily closes an inefficient military camp.** The call site still invokes it every cycle (`src/OAI_MILI.cpp`, `think_close_camp();  // think about closing down an existing one`), but the function body does nothing.
- `src/OAI_MAIN.cpp :: Nation::think_explore()` is an empty function body despite being wired into the once-per-`intervalDays` rotation — it's called, and does nothing.
- Pathfinding heuristic (`src/OSPATH.cpp`, the `SeekPath` search) uses **squared Euclidean distance** compared directly against a linear step cost — the author's own comment says "should really use sqrt()." This makes the heuristic non-admissible (it doesn't consistently underestimate true cost in the same units as the path cost), which can produce suboptimal or erratic-looking long-distance paths.
- `src/OSPATH.cpp :: SeekPath::result_node_distance(ResultNode*, ResultNode*)` (confirmed present, `inline short` at line ~448) is documented by the earlier research pass as containing an X/Y-axis mixup in its distance computation (comparing one node's X coordinate against the other node's Y coordinate) — **this specific line-level claim should be re-verified by hand before Phase 4 touches this file**, since it's a subtle enough bug that a fresh read is warranted rather than trusting the prior summary verbatim.
- `src/OUNITAI.cpp :: Unit::ai_handle_seek_path_fail()` (confirmed present) — after repeated pathfinding failures (order of 5-7, scaled for general-rank units), a unit gives up and is resigned rather than the game attempting a different strategy.

## Rendering / Nausea Root Causes

These seven items are why the game is uncomfortable on modern displays. Each is worse today than it was on a 1997 CRT because CRTs had phosphor persistence and typically ran near 60-70Hz with electron-beam scanning that naturally blurred/smoothed transitions — none of that exists on a crisp, fixed-pixel, often 120Hz+ LCD/OLED panel, so the same underlying code now reads as tearing, judder, or strobing rather than a soft period flicker.

1. **No vsync requested.** `src/OVGA.cpp :: Vga::init()` creates the renderer with `SDL_CreateRenderer(window, -1, 0)` — flags are `0`, so `SDL_RENDERER_PRESENTVSYNC` is never requested. Confirmed by direct grep: the only two references to `SDL_RENDERER_PRESENTVSYNC` anywhere in `src/` are in `src/OVGA.cpp :: Vga::save_status_report()`'s diagnostic dump (writing "V-sync: on/off" to a status file) — i.e. the code *checks* whether vsync happened to be on, it never *asks* for it. There is no config field or options-menu entry for this anywhere in the codebase. Without vsync, the modern display can tear mid-frame; a 1997 CRT's own scan cadence made this largely invisible.
2. **Busy-spin main loop.** `src/OSYS.cpp :: Sys::main_loop()` never sleeps — confirmed zero `SDL_Delay` calls anywhere in `src/`. `Sys::yield()` only polls SDL events/audio/network. A developer's own comment left in the loop acknowledges the gap was known and never fixed. Net effect: the game spins a CPU core at ~100% even when idle, and frame pacing is whatever falls out of an uncontrolled poll loop rather than a deliberate cadence.
3. **Fixed ~17ms presentation gate, unrelated to actual display refresh.** `src/OVGA.cpp :: Vga::flip()` throttles presentation with `cur_ticks > ticks + 17` — a hardcoded wall-clock gate targeting ~58.8fps, with no relationship to the monitor's actual refresh interval (60Hz=16.67ms, 75Hz=13.3ms, 120/144/165Hz even more mismatched). Combined with #1 and #2, this is the most likely single largest contributor to the reported nausea — irregular judder beating against a real, much-faster refresh cycle.
4. **Discrete whole-tile jump-scroll camera, not interpolated.** `src/OMATRIX.cpp :: Matrix::scroll(int xScroll, int yScroll)` moves the viewport by whole-tile integer units (`top_x_loc += xScroll; top_y_loc += yScroll;` — confirmed, no interpolation of any kind), where one tile is 32px (`include/OWORLDMT.h :: ZOOM_LOC_WIDTH`/`ZOOM_LOC_HEIGHT`). Triggered from `src/OWORLD.cpp :: World::detect_scroll()` on its own separate wall-clock timer (`next_scroll_time = misc.get_time() + 500/(config.scroll_speed+1)`, default `scroll_speed`=5 → ~12 jumps/sec). Each scroll step is an instant 32px teleport of everything on screen, once per timer tick — likely the second-largest nausea contributor; jerky non-continuous camera motion is a well-documented motion-sickness trigger, and old CRT blur used to soften what's now a crisp instant jump. **Confirmed safe to smooth without a determinism strategy**: `top_x_loc`/`top_y_loc` is saved (`src/OGFILE2.cpp`, `zoom_matrix->top_x_loc = ...`) but does **not** appear anywhere in `src/OMP_CRC.cpp`'s synced fields (direct grep confirms only unit `stop_x_loc`/`stop_y_loc` is CRC-tracked, an unrelated field). However, screen-coordinate math at multiple draw sites assumes whole-tile alignment — e.g. `src/OFIRMDRW.cpp` computes `srcX = (ZOOM_X1 + (loc_x1 - world.zoom_matrix->top_x_loc) * ZOOM_LOC_WIDTH ...)` directly off the integer tile offset (confirmed by direct read) — plus similar patterns believed present in `src/OF_MARK.cpp` and `src/OSERES.cpp` (not re-verified line-by-line in this pass; verify before implementing sub-pixel interpolation). Any smoothing approach must account for this.
5. **Hardcoded bilinear upscale filter.** `src/OVGA.cpp :: Vga::init()` sets `SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear")` unconditionally. There's no nearest-neighbor/integer-scale choice, so every sprite/tile edge is blurred on upscale to modern resolutions — worse in motion. Note for implementation: this hint is read at texture-creation time, not per frame, so a runtime toggle requires recreating the render texture (the `SDL_CreateTexture` call in `Vga::init()`), not just flipping the hint string.
6. **Palette color-cycling "marching ants" selection highlight.** `src/OANLINE.cpp :: AnimLine::inc_phase()` (confirmed present) advances a palette-index animation phase once per displayed frame, called from the main display path. A classic CRT-era flicker technique for selection/path highlighting — its cadence is currently tied to raw frame rate, so once presentation is decoupled from the current ~58.8fps cap (items 1-3), this could strobe faster than intended unless explicitly decoupled to a fixed real-time interval.
7. **No video/accessibility settings in the in-game Options menu.** Confirmed by direct grep: none of the `ConfigAdv` video fields (`vga_full_screen`, `vga_keep_aspect_ratio`, `vga_window_width`, `vga_allow_highdpi`, etc., declared in `include/ConfigAdv.h`) appear anywhere in `src/OOPTMENU.cpp`. Every fix above needs its own settings-menu exposure to be usable without hand-editing `config.txt`.

**A former item 5, "whole-palette brightness strobe for lightning weather," was removed from this list and Phase 1 dropped from eight rendering items to seven.** Phase 1a investigation found it isn't a live nausea/photosensitivity risk — the brightness-flash code path is dead (see "Lightning brightness strobe: dead code, not a live hazard" under Open Questions below for the full finding). `.claude/commands/phase1a-lightning-strobe.md` has been deleted accordingly; there is no Phase 1a anymore.

## Open Questions — Verify Before Phase 1

**Does `Sys::main_loop()` truly decouple simulation-tick advancement from frame presentation, or is `Vga::flip()`'s ~17ms gate functionally the game's frame limiter today?** This is the single most consequential unresolved question from this research pass — it determines whether fixing items 1-3 above (vsync + real frame pacing) is a small, contained change or one that risks silently speeding up gameplay.

- **Evidence pointing toward already-decoupled:** `src/OSYS.cpp :: Sys::should_next_frame()` gates simulation-tick advancement against `config.frame_speed` (default 12, player-adjustable 0-30 or 99="uncapped", via a slider in `src/OOPTMENU.cpp`) — a wall-clock-derived interval (`next_frame_time = curTime + 1000/config.frame_speed`) that is structurally separate from `Vga::flip()`'s own ~17ms presentation gate. Confirmed: `should_next_frame()` is called from `Sys::main_loop()` to decide whether to advance the sim at all, independent of when `flip()` decides to present. If this separation is real and complete, raising the presentation rate via vsync should not, by itself, speed up the simulation.
- **What would falsify this:** if `World::detect_scroll()`'s edge-scroll timer, `AnimLine::inc_phase()`'s cadence, or any other visible timing/animation logic turns out to be driven by loop-iteration count or by how often `disp_frame()` gets called, rather than by `misc.get_time()`/wall-clock deltas, then raising the iteration rate (via vsync/removing the 17ms gate) would speed that specific logic up regardless of the sim-tick gate holding — a gameplay change disguised as a rendering fix. `World::detect_scroll()`'s timer is itself wall-clock-based (`next_scroll_time = misc.get_time() + ...`), which is a second point of evidence toward decoupling — but this has not been exhaustively checked across every visual subsystem.
- **How this gets settled, not assumed:** Phase 0's wall-clock pacing assertion (N game-days should take ≈T real seconds), run before and after enabling vsync / removing the 17ms gate. If T holds steady, the decoupling evidence above is confirmed. If T shrinks, the falsifying case is real and the fix's scope expands to auditing every wall-clock-vs-iteration-driven timer in the render/update path before shipping.

Do not resolve this by further code reading alone — it needs an empirical run.

**Phase 1d update — the decoupling evidence held, and vsync turned out to be a near no-op while the ~17ms gate stands.** Phase 1d enabled `SDL_RENDERER_PRESENTVSYNC` and ran the Phase 0 pacing assertion before and after. Normal-mode wall time for 50 game-days: **45568 ms baseline vs. 45567 ms with vsync**, with `frame_iters` at 550 in both. The same numbers held on a real display with vsync actually granted (45567 ms) and with it explicitly off (45567 ms). For this specific change the "already-decoupled" evidence above is **confirmed** — enabling vsync does not move simulation pacing.

- **The predicted beat pattern was not observed.** The expectation was that `src/OVGA.cpp :: Vga::flip()`'s ~18 ms effective gate beating against a shorter vblank interval would produce alternating short/long present intervals and visible judder. Measured against a real 75 Hz display (13.33 ms vblank) with the gate logic replicated verbatim: **277 presents in 5 s both with and without vsync; mean interval 17.95 ms (55.7 fps) vs. 18.00 ms (55.6 fps); zero intervals above 20 ms in either configuration.** No doubling, no beat. The mechanism is straightforward in hindsight — the gate period (~18 ms) is *longer* than the vblank interval at both 75 Hz (13.33 ms) and 60 Hz (16.67 ms), so a vblank is essentially always available by the time the gate opens and present rarely has to wait.
- **60 Hz remains unmeasured.** The reference machine has no 60 Hz display (165 Hz and 75 Hz only). Do not record the beat-pattern hypothesis as either confirmed or refuted at 60 Hz — it is untested there, and the 75 Hz result is suggestive but not conclusive for a different refresh rate.
- **Implication for 1e:** the ~17 ms gate, not the absence of vsync, is the binding constraint on presentation rate. Vsync cannot improve smoothness while presentation is already capped below the display's refresh rate. This is why `vga_vsync` ships defaulting to **off** — it is inert-to-marginal today, and only becomes meaningful once 1e replaces the gate with real pacing. Re-run this measurement as part of 1e.
- **SDL gotcha worth not rediscovering:** `SDL_GetRendererInfo()`'s `SDL_RENDERER_PRESENTVSYNC` flag is a snapshot of the flags the renderer was *created* with, and is **not** refreshed by `SDL_RenderSetVSync()` (verified against SDL 2.32 — the flag still reads "on" after a successful `SDL_RenderSetVSync(r, 0)`). It is reliable for reading what creation granted, and useless for reading back a runtime toggle; the call's own return code is the only trustworthy runtime answer.

**Late-game simulation cost may grow unboundedly — candidate Phase 4 investigation, not yet diagnosed.** Phase 0's headless regression harness (`scripts/phase0_harness.sh`) surfaced this as a side effect of building its fast-mode (`-speed 99`, uncapped pacing) regression check: cumulative per-frame simulation cost in the built-in 2-nation test scenario (`Battle::run_test()`, `src/OBATTLE.cpp`) scales roughly linearly with simulated day count up to ~200 days (measured on the reference machine: 50d≈76ms, 100d≈152ms, 200d≈275ms total), then goes non-linear somewhere in the 200-300 day range — one 300-day fast-mode run pegged a CPU core at 100% for minutes with no sign of completing (killed manually; not diagnosed further, no crash or error, just runaway cost).

- **Likely suspects (not verified — this needs its own investigation, not assumed from this list):** unbounded growth in one of the fixed-capacity object arrays (`include/ODYNARR.h`/`ODYNARRB.h` — `unit_array`, `firm_array`, etc., per this doc's "Core object-array architecture" section) as the AI-driven economy/war expands; an O(n²) accumulation somewhere in per-object AI think cycles (`src/OAI_*.cpp`/`OTOWNAI.cpp`/`OUNITAI.cpp`, the ~41-file AI layer described above) scaling with unit/firm count; or pathfinding cost (`src/OSPATH.cpp`) scaling with unit count as more units compete for paths/searches.
- **Why normal play doesn't show this as an obvious hang:** at the default `frame_speed=12`, `should_next_frame()`'s ~83ms/frame wall-clock gate (`src/OSYS.cpp`) absorbs per-frame cost increases up to that budget — the same dilution effect that made Phase 0's original wall-clock-only pacing check nearly blind to simulation-cost regressions (see `scripts/phase0_harness.sh`'s header for that derivation). A player would likely experience this as gradually increasing lag/stutter as a match runs long, not a hard wall, until true per-frame cost exceeds the frame budget outright.
- **Possible connection:** this may be the underlying mechanism behind fan-reported late-game slowdown. Anecdotal — not sourced or verified in this research pass; flag, don't assume.
- **Scope:** not investigated further in Phase 0. Diagnosing which array/subsystem is responsible, and whether it's a real unbounded-complexity bug vs. expected/bounded growth that just happens to look sharp in this narrow scenario, is Phase 4-shaped work (it lives in the AI/simulation logic, which is red-listed). Reproduction recipe: `scripts/phase0_harness.sh --fast-only --days 300` (or run the binary directly with `-speed 99 -headless-test-days 300`) — expect it to hang; kill it manually.

**Lightning brightness strobe: dead code, not a live hazard — should it be repaired at all?** Phase 1a set out to add an accessibility toggle for what this document previously described as "a literal abrupt full-screen brightness flash" on lightning weather, a supposed photosensitivity risk. Investigation found the opposite: the brightness-flash branches in `src/OWORLD_Z.cpp :: ZoomMatrix::draw_weather_effects()` test the member `init_lightning` against ranges 101-107, but within that same function `init_lightning` is only ever assigned `0` or `1` (never touched anywhere else in the file). Those branches are therefore unreachable — `newBrightness` always falls through to the `else` case, `-weather.cloud() * config.cloud_darkness`, which has nothing to do with lightning. `config.lightning_brightness` (`include/OCONFIG.h :: lightning_brightness`) is read into `maxBrightness` and then never used. **The flash does nothing today, regardless of its config value.**

`git log --follow -p -- src/OWORLD_Z.cpp` across all 21 commits that ever touched this file shows the mismatch is not a regression: the block is byte-identical (modulo CRLF normalization) between the original `7c7b7737` "provided by Enlight under the GPL" commit and today. No commit in this repository's history ever changed these lines. As far as this codebase's tracked history goes, the strobe has never fired.

This reframes the open question from "how do we fix a photosensitivity bug" to **"should this ever be made to work at all?"**:
- Implementing it now means *introducing* an effect no player of this codebase has ever seen, not *restoring* a regression — a materially different, higher-scrutiny kind of change.
- It fails the CLAUDE.md bar ("don't redesign systems that aren't broken... core gameplay preserved"): the game's default feel, as every player has actually experienced it, has never included this flash. Making it fire for the first time changes that feel rather than preserving it.
- It's a hard sell for the stated upstream-contribution goal (small, reviewable, plausibly-mergeable commits) — 7kfans maintainers would be asked to accept a *new* visual effect dressed as a bugfix, which invites exactly the "does this change how the game feels" scrutiny the project's own contribution philosophy tries to avoid.
- **If it's ever wanted** (human decision only, not to be bundled with any other fix): the accessibility control was already prototyped and reverted rather than shipped. Shape: a `SlideBar` member (`lightning_flash_slide`) added to `OptionMenu` (`include/OOPTMENU.h`/`src/OOPTMENU.cpp`), positioned alongside the existing volume/frame-speed/scroll-speed sliders, driving the existing `config.lightning_brightness` continuously over 0-60 (0 = off, matching this menu's existing "0 = muted" idiom for the volume sliders) instead of the previous config-file-only 0/20/40/60 steps. No `OCONFIG.cpp`/`OCONFIG.h` changes were needed — `lightning_brightness` was already wired through `Config::change_preference` and the binary `CONFIG.DAT` round-trip. This slider only matters once/if the `init_lightning` dead-code issue above is separately fixed — until then it controls a value nothing reads.

## Cross-Cutting Risk Table

| Finding | Phase | Determinism-sensitive? |
|---|---|---|
| No vsync | 1d | No — presentation only |
| Busy-spin loop / 17ms gate | 1e | Open question above — verify empirically |
| Jump-scroll camera | 1f | No (confirmed: not in `src/OMP_CRC.cpp`) — but draw-site coupling risk |
| Hardcoded bilinear scaling | 1b | No — presentation only |
| Marching-ants cadence | 1c | No — presentation only, but frame-rate coupled |
| No video options menu | 1 (all sub-items) | No |
| Fixed 800×600 internal resolution | 2 | No — presentation, but broad UI touch surface |
| Legacy asset formats | 3 | Low — adjacent to binary I/O conventions shared with saves |
| `think_close_camp`/`think_explore` stubs | 4 | Yes — AI decisions feed CRC-synced unit orders |
| Pathfinding heuristic/typo | 4 | Yes — same reason |
| Cheat-income difficulty model | 4 | Yes — affects balance and CRC-synced economic state |
| ARM/WASM FPU-determinism gap | Cross-cutting, blocks new targets | Yes — explicitly out of scope without a dedicated sub-plan |
| Save-format fragility | Cross-cutting | Yes — any struct layout change |

## Appendix

### File inventory (by subsystem)

See "AI Subsystem Findings" and "Architecture Findings" above for the detailed per-file breakdowns already gathered; not restated here to avoid duplicate, driftable line-count claims.

### TODO/FIXME/HACK/XXX/BUGHERE markers (representative, not exhaustive)

A repo-wide grep across `src/*.cpp`+`include/*.h` found on the order of 90+ such markers. Representative, individually notable ones:
- `src/OBULLET.cpp` and `src/OB_HOMIN.cpp` — both flag a `BUGHERE` about `parentType`/`parentRecno` handling: bullets are only fully supported when owned by a unit, not by a firm/town, described as an acknowledged incomplete generalization.
- `src/OFIRMDIE.cpp` — three separate `BUGHERE` notes about bitmap-loading order and firm-database name/build-code matching.
- `src/OAI_MILI.cpp :: Nation::think_military()` — a `BUGHERE` comment on the camp-expansion town-selection rating (uses raw population, flagged by the author as needing revision).
- `src/OAI_CAP2.cpp` — two `BUGHERE` markers inside **commented-out** code in `enemy_town_combat_level`-adjacent logic (population-based combat padding, and a same-nation check that reads inverted) — i.e. the fix was drafted and then disabled rather than shipped.
- `src/OFIRMAI.cpp` — a `BUGHERE` in `ai_firm_captured`.
- `src/OGENHILL.cpp` — flags an ignored "special or extra flag" case in hill generation.

### Pre-git diff-marker comments

Style like `//### begin alex 3/10 ###//` and `//###### begin Gilbert 23/10 #######//` appear embedded directly in code (e.g. in `src/AM.cpp`, `src/OSYS.cpp`) — these are pre-version-control developer change markers, predating the project's move to git, and are informational archaeology rather than anything requiring action.

### Notable recent bugfix commits (git history, for context — not file citations, not checked by `scripts/check-citations.sh`)

The project has an active history of incremental AI/pathfinding/rendering bugfixing consistent with a maintained compatibility fork rather than a from-scratch rewrite. Recent examples (subject lines paraphrased from `git log`): fixes for a crash on a stale attack-fort reference, a buffer underflow causing erroneous AI town combat-level, uninitialized variables in AI defense-response and tribute/sneak-attack decision logic, an off-by-one in AI town-settling adjacency checks, and units pathfinding to a deleted ship after a save reload. Run `git log --oneline` for the current list; do not treat this appendix as up to date beyond the date this document was written.
