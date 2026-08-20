---
description: 7KAA remaster Phase 0 — build a deterministic-replay + pacing regression harness before any gameplay/rendering changes
---

# Phase 0 — Foundation & Regression Harness

## Preamble (applies to every 7KAA remaster phase command)

1. Read `CLAUDE.md` at the repo root before doing anything else. It states the project's identity (remaster, not rewrite — core gameplay must be preserved), the determinism constraint, and the red list.
2. A `PreToolUse` hook (`scripts/hooks/guard-red-list.sh`) will block edits to save-format, CRC, build-flag, and AI-decision files. If you hit that block, it means stop and ask the human — do not try to work around, disable, or edit the hook itself.
3. This phase produces **tooling only** — no gameplay or rendering behavior should change as a result of Phase 0.
4. If anything below is ambiguous or the codebase doesn't match what's described, stop and report the discrepancy rather than guessing.

## Why this phase exists

Every later phase (especially Phase 1, which touches frame pacing directly) needs a way to prove two different things: "the simulation still produces identical results" (a determinism/correctness check) and "the simulation still runs at the same *speed*" (a pacing check). CRCs alone catch the first but not the second — a simulation that's silently running at 3x speed would still produce matching CRCs if the input stream and frame count are identical, because the same frames run in the same order, just compressed into less wall-clock time. The pacing assertion is what actually guards Phase 1's frame-pacing work; do not treat the CRC check as sufficient on its own.

## Step 1 — Determine headless viability FIRST. Do not build anything else until this is answered.

7KAA initializes SDL video before doing much else, and there is no obvious `--nogui`/headless flag today. Before writing any harness code:

1. Try running the game binary with `SDL_VIDEODRIVER=dummy` (SDL2's built-in no-window video driver) and see what happens — does it start, run, and exit cleanly, or does it fail/hang/crash?
2. Check `src/AM.cpp :: main()` and `src/OVGA.cpp :: Vga::init()` for anything that assumes a real window/display exists (window creation failure handling, DPI queries, etc.) that might need a minimal guard for headless operation.
3. **If `SDL_VIDEODRIVER=dummy` (or an equivalent minimal change) gets a running instance that can complete a game session and exit:** proceed to Step 2.
4. **If headless execution turns out to require substantial engine changes** (e.g. deep coupling between simulation advancement and the render path, or a hard requirement on a live window): **stop here and report this as a scope change.** Do not silently build the rest of Phase 0 on an unverified assumption that headless "basically works." Report what you tried, what broke, and your best estimate of what a minimal headless-enabling change would require, and let the human decide whether to proceed, descope, or find another verification strategy (e.g. running with a real but minimized/off-screen window instead of true headless).

## Step 2 — Single-instance deterministic replay (only after Step 1 succeeds)

Scope note: this is **not** a multi-peer lockstep network test. A full multi-peer test across this codebase's ~90 global singletons is weeks of work on its own and would stall everything else — that's explicitly out of scope for Phase 0. This step only needs one process.

1. Look at the existing replay infrastructure: `src/ReplayFile.cpp`/`include/ReplayFile.h`, and the offline dump tool `tools/rpldump` (which already knows how to decode this format) — reuse this rather than inventing a new recording format.
2. Build a script/harness that: launches the game headless (per Step 1), plays a short scripted scenario (a fixed RNG seed plus either a recorded input stream or a built-in test/AI-only scenario — check `src/OBATTLE.cpp` for existing `Battle::run_test()`/`Battle::run_sim()` entry points, which may already do something close to this for AI-only simulation testing), and runs for a fixed number of in-game days.
3. At the end of the run, compute and print/log a final CRC using the same mechanism `src/OMP_CRC.cpp` uses for multiplayer sync comparison (reuse its CRC computation, don't reimplement it).
4. Check the result in against a baseline CRC value captured from an unmodified run. The harness should compare against this baseline and fail loudly on mismatch.

## Step 3 — Wall-clock pacing assertion

1. Using the same scripted run from Step 2, measure real (wall-clock) elapsed time for the run to complete N in-game days.
2. Assert that this falls within an expected tolerance band (e.g. ±X%) of a baseline timing captured from an unmodified run — this is what actually catches a change that silently speeds up or slows down the simulation, which a CRC-only check cannot catch (see "Why this phase exists" above).
3. Make this assertion easy to re-run standalone (not bundled invisibly inside the CRC check), since later phases — especially `1d`/`1e` (vsync and frame-pacing fixes) — will run this specific check before and after their changes as their primary safety net.

## Definition of done

- A documented, single-command way to run the headless replay + CRC comparison + wall-clock pacing assertion (e.g. a script in `scripts/`).
- A checked-in baseline CRC value and baseline timing value, captured from the current unmodified codebase.
- A short README or header comment in the harness script explaining how to regenerate the baseline if a *deliberate* simulation change is made (this should be a rare, explicit action, not something that happens by accident).
- If Step 1 concluded headless isn't cheaply achievable: a clear written report of what was tried and what a minimal fix would require, with no further Phase 0 work attempted until the human decides how to proceed.
- Report back: did headless work out of the box, with a small change, or not at all? What's the baseline CRC and timing? How do future phases invoke this harness?
