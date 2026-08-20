---
description: 7KAA remaster Phase 1d — request SDL_RENDERER_PRESENTVSYNC with a capability fallback, and expose it as a setting
---

# Phase 1d — Enable Vsync

## Preamble

1. Read `CLAUDE.md` at the repo root first.
2. The red-list hook shouldn't block anything here (confined to `src/OVGA.cpp` + options menu), but this item is the first one that touches the open frame-pacing question in `docs/remaster/FINDINGS.md` — read that section ("Open Questions — Verify Before Phase 1") in full before starting.
3. **The Phase 0 wall-clock pacing assertion is required for this item, not optional.** Unlike `1a`–`1c`, this change plausibly moves how fast the presentation loop iterates, which is exactly the thing the open question is about. If the automated harness genuinely isn't available yet, **stop and escalate to the human** rather than proceeding on manual timing alone — approximate manual timing is not precise enough to catch a subtle speed change here.

## The problem

`src/OVGA.cpp :: Vga::init()` creates the renderer with `SDL_CreateRenderer(window, -1, 0)` — flags are `0`, so `SDL_RENDERER_PRESENTVSYNC` is never requested. The only existing references to that flag are diagnostic (`Vga::save_status_report()` reports whether vsync happened to be on, it never asks for it). There is no config field or options-menu entry for this today.

## What to do

1. Run the Phase 0 harness now, before any change, and record the baseline CRC and pacing numbers.
2. Change renderer creation to request `SDL_RENDERER_PRESENTVSYNC`. SDL2 may fail to honor this depending on the driver — check the result and fall back gracefully (don't crash or silently break rendering if vsync isn't available on a given system). The existing capability-check pattern in `Vga::save_status_report()` (checking `info.flags & SDL_RENDERER_PRESENTVSYNC`) shows how to detect whether it was actually granted; reuse that pattern for the fallback check rather than inventing a new one.
3. Make this configurable (on by default when available, but toggleable) rather than unconditionally hardcoded — wire it into `src/OOPTMENU.cpp`.
4. Do **not** also change `Vga::flip()`'s ~17ms gate in this same item — that's `1e`. Land vsync on its own first so its effect on pacing can be isolated and measured independently. (`Vga::flip()`'s gate will still exist after this change and may interact with vsync in ways worth observing — note anything surprising you see, but don't fix it here.)

## Verification before handing back

1. Run the Phase 0 wall-clock pacing assertion **after** the change and compare against the baseline from step 1.
   - **If pacing (T for N game-days) holds steady**: the "already decoupled" evidence in `docs/remaster/FINDINGS.md`'s Open Questions section is confirmed for this specific change. Proceed.
   - **If pacing shifts noticeably**: the falsifying case from the Open Questions section is real. Stop, do not paper over it, and report exactly what shifted and by how much — this becomes required reading before `1e` starts, since `1e` is even more directly implicated.
2. Confirm the CRC output is unchanged (presentation-only change, should have zero effect on simulation state).
3. Confirm the game runs correctly with vsync unavailable (test by forcing the fallback path if you can, e.g. via a software renderer or an environment that doesn't support it) — it must not crash or hang.
4. Confirm the new setting persists and is togglable from the options menu.

## Hand back to the human

Report the baseline vs. post-change pacing numbers explicitly — this is the most important number in this handoff, not a footnote. State plainly whether the decoupling evidence held or broke. Ask the human to play-test for any perceived change in game speed in addition to the automated numbers, since "the assertion passed" and "it feels the same to a human" are both required, not just one.
