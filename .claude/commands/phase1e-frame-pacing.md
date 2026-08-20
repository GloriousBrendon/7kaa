---
description: 7KAA remaster Phase 1e — remove the busy-spin main loop and the hardcoded 17ms presentation gate, replacing with real pacing
---

# Phase 1e — Real Frame Pacing (Remove Busy-Spin + Fixed 17ms Gate)

## Preamble

1. Read `CLAUDE.md` at the repo root first.
2. Read `docs/remaster/FINDINGS.md`'s "Open Questions — Verify Before Phase 1" section in full, and read the handoff report from `1d` (vsync) if it exists — this item is directly downstream of what that item found.
3. **The Phase 0 wall-clock pacing assertion is mandatory for this item, not optional, and not satisfiable by manual timing.** This is the change most likely to actually move simulation speed if the "already decoupled" evidence turns out to be wrong. If the automated harness genuinely isn't available yet, **stop and escalate to the human** — do not proceed on approximate manual timing for this specific item.
4. This is the largest/riskiest item in Phase 1 apart from `1f`. Take it seriously; do not rush the verification step to "ship the phase."

## The problem

Two separate issues, confirmed independently in `docs/remaster/FINDINGS.md`:
1. `src/OSYS.cpp :: Sys::main_loop()` never sleeps — confirmed zero `SDL_Delay` calls anywhere in `src/`. `Sys::yield()` only polls events/audio/network. This spins a CPU core at ~100% even when idle.
2. `src/OVGA.cpp :: Vga::flip()` throttles presentation with a hardcoded `cur_ticks > ticks + 17` wall-clock gate (~58.8fps), unrelated to the monitor's actual refresh interval.

## What to do

1. Run the Phase 0 harness now, before any change, and record baseline CRC + pacing (or reuse `1d`'s post-change baseline if you're picking this up immediately after that item, and note explicitly which baseline you're using).
2. **Add real idle pacing to the main loop.** `Sys::main_loop()`/`Sys::yield()` should not spin at 100% CPU when there's nothing new to do. Be careful: only the idle-spin/input-poll portion should sleep — this must not throttle or delay actual simulation tick timing (`Sys::should_next_frame()`'s own gate should remain the authority on sim advancement; you're only removing wasted CPU cycles in between, not adding a new artificial delay to the sim).
3. **Replace the hardcoded 17ms `Vga::flip()` gate.** Do not simply change `17` to a different hardcoded constant — that reproduces the same class of bug at a different frequency. Prefer one of:
   - Removing the manual throttle entirely and letting vsync (from `1d`, if landed and available) pace presentation naturally, or
   - If vsync is unavailable/disabled, deriving the interval from the actual detected display refresh rate (`SDL_GetDisplayMode` or equivalent) instead of a hardcoded constant.
4. Keep `Sys::should_next_frame()`/`config.frame_speed` (the simulation-tick gate) untouched in this item — this item is about presentation cadence and idle CPU usage, not simulation speed. If you find yourself needing to touch that function, stop and reconsider — it likely means the "separate timers" assumption from the Open Questions section is wrong, which is exactly the scenario that needs to be reported, not silently worked around.

## Verification before handing back

1. Run the Phase 0 wall-clock pacing assertion **after** the change, compare against the pre-change baseline.
   - **Pacing holds steady**: the decoupling evidence is fully confirmed for the main-loop/flip changes. Proceed.
   - **Pacing shifts**: stop. This is the scenario `docs/remaster/FINDINGS.md`'s Open Questions section explicitly warned about. Do not ship this item with a speed regression. Report exactly what changed, and whether it's specifically `World::detect_scroll()`'s timer, `AnimLine::inc_phase()`'s cadence (should already be fixed by `1c` — verify that fix is still holding), or something else entirely that turned out to be iteration-driven rather than wall-clock-driven. This becomes required investigation before this item can ship.
2. Confirm CRC output is unchanged (this should be a presentation/idle-loop-only change with zero simulation-state impact).
3. Measure CPU usage before and after — idle CPU usage should drop meaningfully from ~100% on a core; report the actual numbers, not just "it seems better."
4. Manual play-test: start a skirmish, play for a few minutes, check for input lag, missed frames, or stutter introduced by the new pacing logic (a sleep that's too coarse can itself introduce input lag — watch for this specifically).

## Hand back to the human

This is the item where "the assertion passed" and "it feels right to a human" are least likely to trivially agree — pacing bugs can be subtle. Report the before/after pacing numbers, before/after CPU usage numbers, and explicitly ask the human to play a session and report on responsiveness/smoothness, not just confirm the numbers look fine. State clearly whether you had to deviate from "don't touch `should_next_frame()`" and why, if so.
