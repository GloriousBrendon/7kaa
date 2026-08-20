---
description: 7KAA remaster Phase 1c — decouple the marching-ants selection-highlight animation cadence from raw frame rate
---

# Phase 1c — Decouple Marching-Ants Cadence From Frame Rate

## Preamble

1. Read `CLAUDE.md` at the repo root first.
2. The red-list hook shouldn't trigger here — this is confined to `src/OANLINE.cpp` and its call site. If it does, stop and ask the human.
3. **Run the Phase 0 regression harness before starting**, if it exists. If not, this item may ship on manual verification alone (see Fallback).
4. This item is sequenced *before* `1d`/`1e` deliberately: once vsync/frame-pacing land, whatever currently drives this animation's rate needs to already be frame-rate-independent, or it will start strobing faster than intended the moment presentation speeds up.

## The problem

`src/OANLINE.cpp :: AnimLine::inc_phase()` advances a palette-index "marching ants" animation phase (used for selection/path highlighting around buildings, units, and paths) once per displayed frame — it's called from the main per-frame display path. Today that's implicitly rate-limited by the game's current ~58.8fps presentation cap (see the rendering root-cause list in `docs/remaster/FINDINGS.md`). Once `1d`/`1e` remove that cap, this animation would speed up in lockstep with the display refresh rate unless it's explicitly decoupled — a classic CRT-era per-frame effect turning into a strobe on a 144Hz panel.

## What to do

1. Read `AnimLine::inc_phase()` and its call site (`src/OSYS2.cpp`, the `anim_line.inc_phase()` call in the main display path) to confirm the current mechanism.
2. Change the advancement to be driven by elapsed wall-clock time rather than "once per call," so the perceived animation speed stays constant regardless of how often the display path is invoked — e.g. accumulate elapsed time and advance the phase only when a fixed real-time interval has passed, matching (or close to) the animation's current perceived speed at today's ~58.8fps cap, so this change alone shouldn't be visibly different from today.
3. Everywhere else this animation is used (`src/OFIRMDRW.cpp`, `src/OTOWNDRW.cpp`, `src/OUNITA.cpp`, `src/OWORLD.cpp`, `src/OWORLD_Z.cpp` are the draw sites referencing the animated color codes per the earlier research pass — confirm this list against a fresh grep before relying on it) should continue to work unmodified, since they only read the current phase/color, not drive its advancement.

## Fallback if the Phase 0 harness isn't available yet

Manual verification is sufficient: observe the marching-ants effect around a selected unit/building/path before and after your change at the game's current frame-pacing settings, and confirm the perceived animation speed is unchanged. (You won't be able to fully test the "does it stay stable once 1d/1e land" property until those items exist — note that limitation in your handoff rather than claiming it's fully proven.)

## Verification before handing back

- Build succeeds.
- If the Phase 0 harness exists: run before/after, confirm identical CRC (this is presentation-only).
- Manual test: confirm marching-ants animation speed looks the same as before your change under current settings.

## Hand back to the human

Report the mechanism you used to decouple the cadence and what real-time interval you targeted. Explicitly note that full confirmation this prevents strobing at higher refresh rates can't happen until `1d`/`1e` land — ask the human to re-check this specific effect once frame-pacing changes are in place, not just now.
