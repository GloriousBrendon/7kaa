---
description: 7KAA remaster Phase 1b — make the render scale-quality filter (bilinear vs nearest-neighbor) a runtime-selectable setting
---

# Phase 1b — Selectable Scaling Filter

## Preamble

1. Read `CLAUDE.md` at the repo root first.
2. The red-list hook won't block anything here — this is confined to `src/OVGA.cpp` and the options menu. If you somehow hit the block, stop and ask the human.
3. **Run the Phase 0 regression harness before starting**, if it exists. If not, this item may ship on manual verification alone (see Fallback) — it cannot plausibly affect simulation speed or determinism.

## The problem

`src/OVGA.cpp :: Vga::init()` sets `SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear")` unconditionally. Every sprite/tile edge is bilinear-blurred when the fixed 800×600 internal image is upscaled to a modern window/display size, with no nearest-neighbor/integer-scale alternative — some players will prefer crisp pixel edges over the current blur, especially in motion.

## This is bigger than it looks — read this before starting

`SDL_HINT_RENDER_SCALE_QUALITY` is read **at texture-creation time**, not per frame. Look at where the render texture is created (`SDL_CreateTexture(..., SDL_TEXTUREACCESS_STREAMING, VGA_WIDTH, VGA_HEIGHT)` in `src/OVGA.cpp :: Vga::init()`) — a runtime toggle means the hint must be set *before* that texture is (re-)created, so changing the setting at runtime requires destroying and recreating the texture (and confirming the renderer/window state survives that correctly), not just calling `SDL_SetHint` again on its own. If you implement this as "just flip the hint and move on," it will silently do nothing until the next full game restart, and you must not report that as working — verify the change actually takes visible effect without a restart before calling this done.

## What to do

1. Add a config field for scaling filter mode (at minimum: nearest / linear; SDL2 also supports `"best"` if you want a third option — your call, state what you picked).
2. Wire it into `src/OOPTMENU.cpp` as a real in-game setting.
3. On change, tear down and recreate the streaming texture with the new hint applied, without requiring a full app restart. Confirm this doesn't leak the old texture or destabilize the renderer.
4. Persist the setting the same way other `ConfigAdv`/`OCONFIG` video-adjacent settings are persisted — check how existing fields like `vga_keep_aspect_ratio` round-trip through `config.txt` and follow that pattern.

## Fallback if the Phase 0 harness isn't available yet

Manual verification is sufficient for this item: change the setting in-game, confirm the visual filter changes immediately (no restart needed), confirm it persists after restart, and confirm the default matches today's current bilinear behavior.

## Verification before handing back

- Build succeeds.
- If the Phase 0 harness exists: run before/after, confirm identical CRC (this change touches nothing simulation-relevant).
- Manual test: toggle between filter modes in the options menu and visually confirm the change takes effect live, without an app restart.
- Confirm the setting persists across restart.

## Hand back to the human

This is a visual-preference change — report which filter modes you implemented, where the setting lives in the options menu, and ask the human to confirm the visual result looks right on their own display before considering it done.
