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

Enforced by a `PreToolUse` hook (`scripts/hooks/guard-red-list.sh`), not just documented here:

- `src/OGFILE2.cpp`, `src/OGFILE3.cpp` — save-format struct layout
- `src/OMP_CRC.cpp` — multiplayer sync CRC computation
- `configure.ac` — determinism build flags
- `src/OSPATH.cpp` — pathfinding (movement outcomes feed simulation state)
- `src/ONATIONB.cpp :: add_cheat()` and difficulty-adjacent balance logic
- `src/OAI_*.cpp`, `src/OTOWNAI.cpp`, `src/OUNITAI.cpp`, `src/OFIRMAI.cpp` — AI decision logic
- `scripts/phase0_harness.sh`, `scripts/phase0_baseline.txt` — the regression harness and its baseline. A failing harness run means something changed; editing the baseline to match an unreviewed result, or loosening the script's checks, silences the safety net instead of fixing what it caught
- `scripts/hooks/guard-red-list.sh`, `.claude/settings.json` — the hook's own protection surface

`src/OCONFIG.cpp` is deliberately **not** red-listed — nearly every phase adds a config toggle there, and red-listing it would mean fighting the hook on almost every commit.

If you hit the block: it means stop and get human sign-off, not find a way around it. There's a `SEVENKAA_REDLIST_ACK` env var escape hatch for authorized sessions (e.g. Phase 4), but it must be set from the *launch* environment before the session starts — never try to set it yourself mid-session.

**The hook only matches the `Edit`/`Write` tools.** It does not intercept Bash — `sed -i`, heredoc redirects (`cat > file <<EOF`), `patch`, `dd`, or any other shell-driven write to a red-listed path is not blocked by it. Never use Bash to modify a red-listed file as a way around this guard; the same human-sign-off requirement applies no matter which tool performs the write.

## Don't break this

Build/economy pacing, combat feel, diplomacy AI personality variety, and the general RTS rhythm fans know. Rendering/AI fixes should change *how* the game reaches an outcome, not *what* outcome feels right by default — flip a legacy-behavior default only after human playtesting confirms it, never as part of the same change that introduces it.

## Rendering pipeline, in one paragraph

Still an 8-bit paletted software renderer: 800×600 internal resolution by default, now a runtime value (`include/OVGA.h :: VGA_WIDTH`/`VGA_HEIGHT` name `vga_buf_width`/`vga_buf_height`; `VGA_LEGACY_WIDTH`/`VGA_LEGACY_HEIGHT` hold the historical constants), CPU-side blitting, one texture upload to SDL2 per frame (`src/OVGA.cpp :: Vga::flip()`). Optional vsync (`vga_vsync`, off by default), an idle-napping main loop, and a presentation interval derived from the display's real refresh rate (`src/OVGA.cpp :: Vga::update_present_interval()`) — all three were the busy-spin/hardcoded-~17ms-gate suspects behind fan-reported nausea/headaches, addressed in Phases 1d and 1e. The remaining suspect is the jump-scroll camera (Phase 1f). Phase 2 added an opt-in wide map viewport (`vga_wide_viewport`, default off): the buffer grows to the window size and sprites stay at native 32px, so more tiles are visible rather than the same tiles magnified. Two coordinate spaces now coexist — the runtime map viewport (`include/OWORLDMT.h :: ZOOM_X1` etc., backed by `zoom_win_x1`…) and the fixed `ZOOM_LEGACY_X1`… that report/dialog screens lay their `enum` tables out against. Full-screen menus still draw in the buffer's top-left 800×600 and are presented from there (`src/OVGA.cpp :: Vga::set_legacy_present`), because they hit-test against hardcoded coordinates. Full detail: `docs/remaster/FINDINGS.md`.

## Build & run

```
./autogen.sh && ./configure && make
SKDATA=data src/7kaa
```
GCC 4.6+/C++11, SDL2 2.24.0+, ENet 1.3.x, OpenAL-soft required; libcurl/gettext optional. See `README` for full dependency list. Once `.claude/commands/phase0-harness.md` has been run once, its script under `scripts/` is the regression check to run before/after any change — see that command for details.

Useful `configure` flags (`configure.ac`): `--enable-debug` (debugging features, off by default), `--disable-curl`/`--disable-enet` (drop optional deps), `--disable-multiplayer` (hide MP from the menu), `--enable-asm` (x86 asm optimizations, opt-in). No linter is configured in this repo, and no unit test suite exists yet — `/phase0-harness` is what creates the first regression script under `scripts/`; before that's been run, a successful build is the only pre-existing correctness signal.

## Core object-array architecture

Game entities aren't heap-allocated individually — each type has a single fixed-capacity manager built on `include/ODYNARR.h :: DynArray`/`include/ODYNARRB.h :: DynArrayB`: `include/OUNIT.h :: unit_array`, `include/OFIRMA.h :: firm_array`, `include/OTOWN.h :: town_array`, `include/ONATIONA.h :: nation_array`. Every live object is reached by its 1-based `recno` into the matching array, not by pointer — this is why so much code threads bare `int recno` through function signatures, and it's what save/load and multiplayer sync key off of. The frame tick is driven by `src/OSYS.cpp :: Sys::main_loop`: input → AI think (`OAI_*`) → per-manager simulation step → render (`OVGA*`/`OSPRITE*`) → multiplayer sync (`OMP_*`).

## O-prefix naming legend

Files are 8.3-style, prefixed `O` for "Object": `OFIRM*` = buildings/production, `OUNIT*` = units, `OTOWN*` = towns/population, `ONATION*` = nations/kingdoms, `OSYS*` = engine/main loop, `OVGA*`/`OSPRITE*` = rendering, `OAI_*` = nation-level AI, `O*AI.cpp` (e.g. `src/OUNITAI.cpp`) = per-object-type AI hooks, `OSPATH*`/`OSPRE*` = pathfinding, `OGFILE*` = save/load, `OMP_*` = multiplayer sync, `OCONFIG*`/`ConfigAdv*` = settings.

## Abbreviations that matter

`loc` = map tile location/coordinate · `recno` = record number (array index into an object-array manager) · `firm` = production building · `nation` = a kingdom/AI or human player · `town` = population center · `CRC` = the multiplayer sync checksum, see `src/OMP_CRC.cpp` · `zoom_matrix`/`top_x_loc` = camera/viewport position · `ai_aggressiveness` = the difficulty-driven AI think-frequency and target-bias knob · `cheat` (in `add_cheat()`) = literally what it says — hidden free income given to AI nations, not a euphemism.
