---
description: 7KAA remaster Phase 1a — add an accessibility toggle for the lightning full-palette brightness strobe (photosensitivity fix, smallest Phase 1 item, ships first)
---

# Phase 1a — Lightning Brightness-Strobe Accessibility Toggle

## Preamble

1. Read `CLAUDE.md` at the repo root first. It states the project's identity (remaster, not rewrite), the determinism constraint, and the red list.
2. A `PreToolUse` hook blocks edits to save-format/CRC/build-flag/AI-decision files. None of those are touched by this item — you should not hit the block. If you do, stop and ask the human rather than trying to work around it.
3. **Run the Phase 0 regression harness before starting** (`/phase0-harness` if not already set up in this session — check for a script under `scripts/` first). If the harness genuinely doesn't exist yet, this item may still ship on manual verification alone (see "Fallback" below) — it does not plausibly move simulation speed or determinism, so it's lower-stakes than `1d`/`1e`.
4. This is presentation-layer work — you are not expected to touch simulation logic at all.

## Why this ships first

This is the smallest item in Phase 1 (~20 lines of actual logic change) and addresses a genuine photosensitivity hazard, not just a comfort complaint — it should not wait behind the larger, riskier frame-pacing/camera-smoothing items.

## The problem

`src/OVGA.cpp :: Vga::adjust_brightness(int changeValue)` adds/subtracts a delta across all 256 entries of the active palette and reapplies the whole palette in one call. It's invoked from the weather-update path in `src/OWORLD_Z.cpp` on lightning events, with the brightness delta driven by `config.lightning_visual`/`config.lightning_brightness` (default max delta 20, configurable to 0/20/40/60 — but only by hand-editing `config.txt`, no in-game toggle). The result is a literal abrupt full-screen brightness flash, tied to the low-frequency logic tick rather than eased — a real seizure/migraine trigger for photosensitive players, currently with zero in-game accessibility control.

## What to do

1. Read the lightning-brightness call site in `src/OWORLD_Z.cpp` in full to understand exactly how `newBrightness` gets computed and when `Vga::adjust_brightness()` is invoked — confirm the mechanism described above still matches current code before changing anything.
2. Add a genuine in-game accessibility control, not just a config.txt-only flag. At minimum: an on/off toggle for the flash effect, and ideally an intensity cap (e.g. limit the effective brightness delta, or ease the transition instead of an instant jump) reachable from `src/OOPTMENU.cpp`.
3. **Do not remove the lightning effect outright.** It's part of the game's weather system — the fix is to cap/soften/toggle it, not delete it.
4. Follow the existing `ConfigAdv`/`Config` precedent (e.g. `include/ConfigAdv.h :: fix_path_blocked_by_team` as a model of a toggle that defaults to legacy behavior) — but note this specific toggle is a presentation/accessibility setting a player should be able to change live in the options menu, not just a save-compatibility flag, so it likely belongs alongside the other `config.txt`-backed gameplay settings (`src/OCONFIG.cpp`) rather than `ConfigAdv`. Use your judgment on which config system fits, and say which you picked and why in your final report.
5. Default: preserve today's default intensity (don't silently make the game "safer by default" in a way that changes the out-of-box experience) unless you have a specific reason to change the default — flag that decision explicitly if you deviate.

## Fallback if the Phase 0 harness isn't available yet

This item may ship on manual verification alone: launch the game, trigger a lightning weather event (check `src/OWORLD_Z.cpp` for the conditions that trigger it, or use a debug/test scenario if one exists), confirm the new toggle actually suppresses/softens the flash, and confirm the default (untoggled) behavior is visually unchanged from before your edit. You do not need the automated CRC/pacing harness for this item.

## Verification before handing back

- Build succeeds.
- If the Phase 0 harness exists: run it before and after; confirm identical CRC output (proves zero simulation-state impact — this change should not touch anything CRC-relevant).
- Manual test: trigger lightning with the toggle off (should match today's current behavior) and with the toggle on (should be visibly softened/absent).
- Confirm the setting persists across a save/reload and a game restart.

## Hand back to the human

**You cannot fully verify this fix yourself.** Whether the softened/toggled effect actually resolves the photosensitivity concern, and whether it still "feels like lightning" rather than feeling broken, is a subjective judgment only a human playtester can make. End this task by reporting exactly what you changed (files, the new setting's name and location, default value) and explicitly ask the human to play-test it before it's considered done.
