# 7KAA Remaster — Agent Reference

Read this before touching any code in this repo. Full depth and citations: `docs/remaster/FINDINGS.md`. Phase-by-phase work: `.claude/commands/phase*.md` (run via `/phase0-harness`, `/phase1a-lightning-strobe`, etc.).

**Built-in `Explore`/`Plan` subagents do not auto-load this file.** If you delegate to one, restate the determinism constraint and red list directly in the delegation prompt. The project-scoped `sevenkaa-explorer` agent (`.claude/agents/sevenkaa-explorer.md`) already has them in its own system prompt.

## Project identity

**This is a remaster, not a rewrite.** 7 Kingdoms: Ancient Adversaries is a beloved, unique 1997 RTS. The goal is a fresh coat of paint, modernized AI, and fixes for the CRT-era rendering that gives fans headaches on modern monitors — with **core gameplay preserved**. Don't redesign systems that aren't broken. When in doubt, keep the feel and change the mechanism underneath it.

**Upstream constraint:** every change ships as a small, toggled, independently reviewable commit that could plausibly go upstream to the 7kfans maintainers. This is what keeps this from becoming a dead fork, and it's why rendering work is split into six single-item phase commands instead of one big one — small reviewable diffs, not big-bang rewrites.

## The determinism / lockstep constraint — read this first

Multiplayer is lockstep: every peer runs identical simulation from identical inputs, checked via per-object CRC comparison in `src/OMP_CRC.cpp`. If a value isn't written into a struct there, it isn't part of the sync contract. The build enforces bit-reproducible floats only *partially* — `-fsigned-char` is mandatory (build fails without it), but `-mfpmath=387 -ffloat-store` (`configure.ac`) is applied only if a compiler probe for 387-FPU support succeeds, and silently skipped otherwise. **Non-x86 targets (ARM/WASM) get no equivalent protection today** — treat this as an open problem, never as solved.

**Is my change safe? Checklist — ask before editing anything simulation-adjacent:**
- Does this value get written into a struct in `src/OMP_CRC.cpp`? → sync-relevant.
- Does this affect the order or content of unit/nation/AI decisions? → those decisions drive synced state even if the code itself isn't in `src/OMP_CRC.cpp`.
- Does this touch floating-point math, RNG order, or struct layout used by saves/network packets?
- Any "yes" → needs a `ConfigAdv`-style toggle defaulting to legacy behavior (see `include/ConfigAdv.h :: fix_path_blocked_by_team` as the model), not a silent change.

**Worked example:** `src/OGFILE2.cpp :: Matrix::top_x_loc` (and `top_y_loc`, camera position) is saved but never appears in `src/OMP_CRC.cpp` — safe to smooth without a determinism strategy. AI decision logic in `src/OAI_*.cpp` drives unit orders, which *are* synced — never safe to change without a toggle.

## Red list — do not edit without explicit human sign-off

Enforced by a `PreToolUse` hook (`scripts/hooks/guard-red-list.sh`), not just documented here. This list and the hook's `RED_LIST` array are in sync as of 2026-09-05 — keep them that way when you add an entry to either. **The hook still does not see Bash**; see **Enforcement scope** below.

- `src/OGFILE2.cpp`, `src/OGFILE3.cpp` — save-format struct layout
- `src/OMP_CRC.cpp` — multiplayer sync CRC computation
- `configure.ac` — determinism build flags
- `src/OSPATH.cpp` — pathfinding (movement outcomes feed simulation state)
- `src/ONATIONB.cpp` — the **whole file** is blocked, not just `add_cheat()`; it holds the cheat-income and difficulty-adjacent balance logic, and the hook matches on path, not on function
- `src/OAI_*.cpp`, `src/OTOWNAI.cpp`, `src/OUNITAI.cpp`, `src/OFIRMAI.cpp` — AI decision logic
- `scripts/phase0_harness.sh`, `scripts/phase0_baseline.txt` — the regression harness and its baseline. A failing harness run means something changed; editing the baseline to match an unreviewed result, or loosening the script's checks, silences the safety net instead of fixing what it caught
- `scripts/phase4_ai_harness.sh`, `scripts/phase4_ai_baseline.txt` — the AI-behaviour harness and its baseline, protected for the same reason as the Phase 0 pair. It carries extra weight because it is the *only* signal that catches an AI regression the multiplayer CRC cannot see: that CRC hashes `NationBase` only, never the AI state in `Nation`
- `scripts/hooks/guard-red-list.sh`, `.claude/settings.json`, `.claude/settings.local.json` — the hook's own protection surface. `settings.local.json` matters as much as the other two and is easy to overlook: it is **untracked** (`.gitignore:112`) and takes **precedence** over `settings.json`, so an edit there is both invisible to review and authoritative. An agent that cannot get past a block can use it to grant itself blanket `Bash` permission — and Bash is not intercepted by this hook at all (see below), so that single edit retires the entire red list

`src/OCONFIG.cpp` is deliberately **not** red-listed — nearly every phase adds a config toggle there, and red-listing it would mean fighting the hook on almost every commit.

If you hit the block: it means stop and get human sign-off, not find a way around it. There's a `SEVENKAA_REDLIST_ACK` env var escape hatch for authorized sessions (e.g. Phase 4), but it must be set from the *launch* environment before the session starts — never try to set it yourself mid-session. Note that a session started from the **VS Code extension** does not inherit a shell's environment, so the escape hatch is unavailable there even if you launched a terminal that way: the block is real and the answer is human sign-off, not a workaround.

### Enforcement scope

The three path-matching gaps found on 2026-09-03 were closed the same day (hook rewritten under `SEVENKAA_REDLIST_ACK` sign-off; re-probed with 178 crafted `PreToolUse` payloads across all 15 red-listed paths, 0 allowed — previously 94 of 118 spellings passed straight through):

- Paths are now canonicalised before matching. The incoming `file_path` is made absolute against the repo root, then `.`, `..`, and repeated/trailing slashes are collapsed and symlinks resolved via `realpath -m` (with a lexical bash fallback if `realpath` is missing). `./src/OMP_CRC.cpp`, `src/../src/OMP_CRC.cpp`, `src//OMP_CRC.cpp` and their absolute equivalents all block. A relative path is resolved against the **repo root**, not the hook process's cwd, which is not part of the hook contract.
- `scripts/phase0_harness.sh`, `scripts/phase0_baseline.txt` and `.claude/settings.local.json` are in `RED_LIST`. `scripts/phase4_ai_harness.sh` and `scripts/phase4_ai_baseline.txt` joined them on 2026-09-05 and were re-probed the same way (18 payloads across the two paths, 0 allowed; ack exemption still honoured).
- `NotebookEdit` is also covered: the `settings.json` matcher `Edit|Write` is an unanchored regex, so it fires for `NotebookEdit` too, which names its target `notebook_path` — the hook reads both fields rather than falling through to allow.

**What the hook still does not cover — unchanged, and structural:**

**The hook only matches the `Edit`/`Write` tools.** It does not intercept Bash — `sed -i`, heredoc redirects (`cat > file <<EOF`), `patch`, `dd`, or any other shell-driven write to a red-listed path is not blocked by it. Never use Bash to modify a red-listed file as a way around this guard; the same human-sign-off requirement applies no matter which tool performs the write. This is why an authorized session should still edit red-listed files with `Edit`/`Write`: it routes the write through the guard and makes the `SEVENKAA_REDLIST_ACK` grant the thing that permits it, rather than sidestepping the check entirely.

Closing the Bash hole needs a second `PreToolUse` hook on `Bash` that parses commands for write redirections and in-place editors — not attempted, since command parsing is far easier to evade than path matching and a partial version would be worse than a documented absence.

## Don't break this

Build/economy pacing, combat feel, diplomacy AI personality variety, and the general RTS rhythm fans know. Rendering/AI fixes should change *how* the game reaches an outcome, not *what* outcome feels right by default — flip a legacy-behavior default only after human playtesting confirms it, never as part of the same change that introduces it.

## Rendering pipeline, in one paragraph

Still an 8-bit paletted software renderer: 800×600 internal resolution by default, now a runtime value (`include/OVGA.h :: VGA_WIDTH`/`VGA_HEIGHT` name `vga_buf_width`/`vga_buf_height`; `VGA_LEGACY_WIDTH`/`VGA_LEGACY_HEIGHT` hold the historical constants), CPU-side blitting, one texture upload to SDL2 per frame (`src/OVGA.cpp :: Vga::flip()`). Optional vsync (`vga_vsync`, off by default), an idle-napping main loop, and a presentation interval derived from the display's real refresh rate (`src/OVGA.cpp :: Vga::update_present_interval()`) — all three were the busy-spin/hardcoded-~17ms-gate suspects behind fan-reported nausea/headaches, addressed in Phases 1d and 1e. The remaining suspect is the jump-scroll camera (Phase 1f). Phase 2 added an opt-in wide map viewport (`vga_wide_viewport`, default off): the buffer grows to the window size and sprites stay at native 32px, so more tiles are visible rather than the same tiles magnified. Two coordinate spaces now coexist — the runtime map viewport (`include/OWORLDMT.h :: ZOOM_X1` etc., backed by `zoom_win_x1`…) and the fixed `ZOOM_LEGACY_X1`… that report/dialog screens lay their `enum` tables out against. Full-screen menus still draw in the buffer's top-left 800×600 and are presented from there (`src/OVGA.cpp :: Vga::set_legacy_present`), because they hit-test against hardcoded coordinates. A fourth pacing suspect was found and fixed after Phase 2: `src/openal/openal_audio.cpp :: OpenALAudio::yield()` presented a frame as a side effect of audio housekeeping (a `VgaFrontLock` whose ctor reaches `Vga::flip()`), so ~98% of all `flip()` calls came from the audio path at a rate set by unit count — and, because `Sys::yield()` is also reached from inside `Sys::process()`, those presents blocked the simulation wherever `SDL_RenderPresent()` blocks. Behind `vga_audio_yield_flip`, default on (legacy). **Timing caveat: numbers in `FINDINGS.md` predating 2026-09-03 were measured on SDL2 proper; this machine runs `sdl2-compat` over SDL3, where a vsync-granted present blocks in `nanosleep` instead of returning immediately — timing conclusions do not transfer between the two.** Full detail: `docs/remaster/FINDINGS.md`.

## Build & run

```
./autogen.sh && ./configure && make
SKDATA=data src/7kaa
```
GCC 4.6+/C++11, SDL2 2.24.0+, ENet 1.3.x, OpenAL-soft required; libcurl/gettext optional. See `README` for full dependency list. Once `.claude/commands/phase0-harness.md` has been run once, its script under `scripts/` is the regression check to run before/after any change — see that command for details. Every harness run pins an **empty** `config.txt` in its own scratch `$SKCONFIG` and asserts, via the `HARNESS_CONFIG_PATH` field, that the game actually loaded that file: `src/ConfigAdv.cpp :: ConfigAdv::load()` otherwise falls back to a bare relative `config.txt`, which resolves inside the data dir `Sys::chdir_to_game_dir()` has already `chdir`'d into — so an untracked `data/config.txt` silently fed personal play-test settings into every timing measurement between 2026-08-21 and 2026-09-03. Pinning `$SKCONFIG` alone is not isolation; if you write a new measurement script, plant the empty file too.

**Two harnesses now exist, and they answer different questions — run both.** `scripts/phase0_harness.sh` (scenario `Battle::run_test()`, flag `-headless-test-days`) is the **determinism and pacing anchor**: 50 days, CRC 37, plus a wall-clock tolerance band and a pacing floor. It ends on day 263 by design and is never modified. `scripts/phase4_ai_harness.sh` (scenario `Battle::run_ai_test()`, flag `-headless-ai-days`, baseline `scripts/phase4_ai_baseline.txt`) is the **AI behaviour** harness, added for Phase 4: an all-AI economy game built through `create_pregame_object()`, 2000 days against a natural end of day 15,411, emitting a per-day CRC series, a per-day per-nation metric series and an end-of-run summary from `src/OAIHARN.cpp`. Both signals are needed because the multiplayer CRC hashes only `NationBase` and never the AI state in `Nation` — see the two 2026-09-05 entries in `docs/remaster/FINDINGS.md`. Nothing in `src/OAI_*.cpp` was touched to produce the metrics, and nothing there should be: instrumenting the code under test is what that design avoids. **`scripts/phase4_ai_harness.sh` and `scripts/phase4_ai_baseline.txt` are red-listed and hook-enforced** (added 2026-09-05, under `SEVENKAA_REDLIST_ACK` sign-off) for the same reason the Phase 0 pair is listed — editing a baseline to match an unreviewed result silences the safety net.

Useful `configure` flags (`configure.ac`): `--enable-debug` (debugging features, off by default), `--disable-curl`/`--disable-enet` (drop optional deps), `--disable-multiplayer` (hide MP from the menu), `--enable-asm` (x86 asm optimizations, opt-in). No linter is configured in this repo, and no unit test suite exists yet — `/phase0-harness` is what creates the first regression script under `scripts/`; before that's been run, a successful build is the only pre-existing correctness signal.

## Core object-array architecture

Game entities aren't heap-allocated individually — each type has a single fixed-capacity manager built on `include/ODYNARR.h :: DynArray`/`include/ODYNARRB.h :: DynArrayB`: `include/OUNIT.h :: unit_array`, `include/OFIRMA.h :: firm_array`, `include/OTOWN.h :: town_array`, `include/ONATIONA.h :: nation_array`. Every live object is reached by its 1-based `recno` into the matching array, not by pointer — this is why so much code threads bare `int recno` through function signatures, and it's what save/load and multiplayer sync key off of. The frame tick is driven by `src/OSYS.cpp :: Sys::main_loop`: input → AI think (`OAI_*`) → per-manager simulation step → render (`OVGA*`/`OSPRITE*`) → multiplayer sync (`OMP_*`).

## O-prefix naming legend

Files are 8.3-style, prefixed `O` for "Object": `OFIRM*` = buildings/production, `OUNIT*` = units, `OTOWN*` = towns/population, `ONATION*` = nations/kingdoms, `OSYS*` = engine/main loop, `OVGA*`/`OSPRITE*` = rendering, `OAI_*` = nation-level AI, `O*AI.cpp` (e.g. `src/OUNITAI.cpp`) = per-object-type AI hooks, `OSPATH*`/`OSPRE*` = pathfinding, `OGFILE*` = save/load, `OMP_*` = multiplayer sync, `OCONFIG*`/`ConfigAdv*` = settings.

## Abbreviations that matter

`loc` = map tile location/coordinate · `recno` = record number (array index into an object-array manager) · `firm` = production building · `nation` = a kingdom/AI or human player · `town` = population center · `CRC` = the multiplayer sync checksum, see `src/OMP_CRC.cpp` · `zoom_matrix`/`top_x_loc` = camera/viewport position · `ai_aggressiveness` = the difficulty-driven AI think-frequency and target-bias knob · `cheat` (in `add_cheat()`) = literally what it says — hidden free income given to AI nations, not a euphemism.
