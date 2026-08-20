---
description: 7KAA remaster Phase 1f — smooth the discrete jump-scroll camera (largest, riskiest Phase 1 item; presents a design decision to the human)
---

# Phase 1f — Smooth Camera Scroll

## Preamble

1. Read `CLAUDE.md` at the repo root first.
2. This is the largest and riskiest item in Phase 1 — not because of determinism (camera position is confirmed *not* CRC-synced, see below), but because of the number of draw sites that assume whole-tile-aligned coordinates.
3. **Run the Phase 0 regression harness before starting**, if it exists. This item's risk is correctness/visual (draw-site alignment), not pacing, so the CRC comparison matters more here than the wall-clock assertion — but run both if available.
4. **Do not unilaterally pick between Option A and Option B below.** Present both to the human with your assessment and let them decide before you implement either, unless they've already told you which one to build.

## The problem

`src/OMATRIX.cpp :: Matrix::scroll(int xScroll, int yScroll)` moves the camera by whole-tile integer units instantly (`top_x_loc += xScroll; top_y_loc += yScroll;` — confirmed, no interpolation), where one tile is 32px (`include/OWORLDMT.h :: ZOOM_LOC_WIDTH`/`ZOOM_LOC_HEIGHT`). It's triggered from `src/OWORLD.cpp :: World::detect_scroll()` on its own wall-clock timer (`next_scroll_time = misc.get_time() + 500/(config.scroll_speed+1)`, default ~12 jumps/sec at `scroll_speed`=5). Every scroll step is an instant 32px teleport of the entire visible world, once per timer tick — this is the likely second-largest nausea contributor after frame pacing (see `docs/remaster/FINDINGS.md`).

## Confirmed safe to change without a determinism strategy

`top_x_loc`/`top_y_loc` is written to save games (`src/OGFILE2.cpp`, `zoom_matrix->top_x_loc = map_matrix->cur_x_loc;`) but does **not** appear in `src/OMP_CRC.cpp`'s synced fields (only unrelated unit `stop_x_loc`/`stop_y_loc` is CRC-tracked). Camera position is a client-local convenience value, not lockstep-synced game state. You do not need a `ConfigAdv`-style multiplayer-compatibility toggle for correctness reasons — but per project convention, still make this configurable/toggleable so it can be turned off if a player strongly prefers the old jump-scroll feel, or if it turns out to interact badly with something.

## The real risk: draw-site coordinate alignment

Multiple draw call sites compute screen position directly from the integer tile offset — confirmed in `src/OFIRMDRW.cpp`: `srcX = (ZOOM_X1 + (loc_x1 - world.zoom_matrix->top_x_loc) * ZOOM_LOC_WIDTH ...)`. Similar patterns are believed present in `src/OF_MARK.cpp` and `src/OSERES.cpp` per the earlier research pass (not re-verified line-by-line — **re-verify this list with a fresh grep for `top_x_loc`/`top_y_loc` across `src/` before you start**, since an incomplete list here means missed draw sites and visible rendering glitches). Any smoothing approach must account for every one of these.

## Present this decision to the human before implementing

**Option A — smaller change, lower risk:** Keep `top_x_loc`/`top_y_loc` as whole-tile integers (required, since the draw sites above assume alignment), but increase scroll granularity: call `scroll()` far more often with smaller deltas, driven by frame time rather than the current fixed `500/(scroll_speed+1)`ms discrete-jump timer. This produces a much higher frequency of smaller jumps, which reads as smooth even without true sub-pixel interpolation, without touching any draw site.

**Option B — larger change, true smoothness:** Introduce a separate sub-tile pixel offset (float or fixed-point) used only at final blit/compositing time, decoupled from the integer `top_x_loc`/`top_y_loc` used for game logic and save compatibility, eased toward the logical target each frame. This requires touching every draw call site that computes screen position from `top_x_loc`/`top_y_loc` (confirmed: `src/OFIRMDRW.cpp`; likely: `src/OF_MARK.cpp`, `src/OSERES.cpp`, and whatever else your fresh grep turns up) to add the sub-pixel offset, not just `Matrix::scroll()`.

Report your recommendation (which option, and why, given what you find when you re-verify the draw-site list) but let the human make the final call before you implement.

## What to do (once a direction is chosen)

1. Confirm the save round-trip still works: `top_x_loc`/`top_y_loc` written at `src/OGFILE2.cpp` should still be the same kind of whole-tile-integer value after your change (Option A keeps this trivially true; Option B must ensure the *logical* integer position, not the smoothed render offset, is what gets saved).
2. Implement the chosen option behind a togglable setting in `src/OOPTMENU.cpp`, defaulting to the new smooth behavior but switchable back to legacy jump-scroll.
3. Re-verify the full list of draw sites reading `top_x_loc`/`top_y_loc` and confirm each one still renders correctly (Option A: trivially, since alignment is preserved; Option B: each site needs to be checked individually for the sub-pixel offset).

## Verification before handing back

- Build succeeds.
- Run the Phase 0 harness (CRC + pacing) before and after; CRC should be unchanged (camera isn't sync state). Pacing should also be unaffected (camera smoothing doesn't touch sim-tick timing) — if pacing shifts, something unexpected is happening and needs investigation before shipping.
- Load an existing save game and confirm the camera position restores correctly.
- Manually scroll in all four directions (and diagonals, and edge-of-screen auto-scroll) and visually confirm no rendering artifacts/misalignment at any draw site touched.

## Hand back to the human

**You cannot verify "does this actually feel less nauseating" yourself** — that's the entire point of this phase and it's subjective. Report which option you implemented and why, the full list of draw sites you touched or confirmed unaffected, and explicitly ask for play-testing focused on scroll feel before this is considered done.
