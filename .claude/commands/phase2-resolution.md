---
description: 7KAA remaster Phase 2 — break the hardcoded 800x600 internal resolution and add a real in-game video settings menu
---

# Phase 2 — Resolution & UI Modernization

## Preamble

1. Read `CLAUDE.md` at the repo root first, and read `docs/remaster/FINDINGS.md`'s Architecture and Rendering sections.
2. This phase should only start after Phase 1 has landed and been human-verified — it builds on the settings-menu plumbing Phase 1 introduces (vsync toggle, scaling filter, etc. in `src/OOPTMENU.cpp`).
3. Presentation-layer work, same determinism profile as Phase 1 — the red-list hook shouldn't trigger, but this phase has a much broader touch surface than any single Phase 1 item (potentially dozens of UI screens), so go carefully and incrementally rather than attempting it in one pass.
4. **Run the Phase 0 regression harness before and after.**

## The problem

Internal render resolution is hardcoded: `include/OVGA.h :: VGA_WIDTH` (800) / `VGA_HEIGHT` (600) / `VGA_PALETTE_SIZE` (256). All game rendering happens into a fixed-size 8-bit indexed surface that's then converted to RGB and upscaled to whatever window/display size via `SDL_RenderCopy` in `src/OVGA.cpp :: Vga::flip()`. Windowed mode is forced to match this native 800×600 size (`src/OVGA.cpp :: Vga::init_window_size()`); fullscreen-desktop mode only auto-picks between a small set of fixed sizes (640×480/800×600/1024×768), never rendering natively at the display's actual resolution.

## Decision point — present to the human before implementing

**Option A — logical-resolution scaling layer (lower risk, recommended default):** Keep the internal 800×600 8-bit logical coordinate space for game logic and most drawing, but render to a higher-resolution target and let scaling handle the rest, similar to today's mechanism but with better filtering/DPI awareness options (building on Phase 1's `1b` scaling-filter work). Confine changes mostly to `src/OVGA.cpp`/`include/OVGA.h`.

**Option B — full UI coordinate rewrite (higher effort, higher payoff):** Rework the UI/HUD coordinate math across every screen to be resolution-independent rather than hardcoded against 800×600. Touches far more files (every `O*.cpp` screen/dialog file that positions widgets in absolute pixel coordinates).

Given the project's core constraint — remaster, not rewrite, preserve the feel fans love — Option A is the safer default recommendation, since UI layout and proportions are part of that feel. Do not choose Option B unilaterally; confirm with the human first, since it's a much larger commitment.

## What to do (once a direction is confirmed)

1. Add real in-game video settings to `src/OOPTMENU.cpp`: resolution/window size, fullscreen mode, and anything else Phase 1 left as config-only. Every new setting should be toggleable and default to preserving today's out-of-box behavior unless there's a specific reason to change it.
2. Implement the chosen scaling approach behind a `ConfigAdv`-style flag defaulting to legacy behavior, per the project-wide toggle precedent (`include/ConfigAdv.h :: fix_path_blocked_by_team` as the model).
3. Test at common modern resolutions/aspect ratios (at minimum 1080p and 1440p, both 16:9 and any ultrawide you can test) — check every menu/dialog for clipped or misaligned widgets, not just the main gameplay view.

## Verification before handing back

- Build succeeds; Phase 0 harness passes (CRC unchanged — this is presentation-only).
- Before/after screenshots at multiple resolutions for human review.
- All menus/dialogs legible and correctly laid out at tested resolutions.
- Existing saves still load correctly.

## Hand back to the human

Report which option you implemented, what resolutions you tested, and any screens/dialogs that still have layout issues at non-800×600 sizes. Ask for visual review via the screenshots before considering this phase done — resolution/UI changes are exactly the kind of thing that looks fine in a build log and wrong on screen.
