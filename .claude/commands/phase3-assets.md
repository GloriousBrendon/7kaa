---
description: 7KAA remaster Phase 3 — modernize legacy .SPR/.ICN/.COL/.RES asset formats via a compatibility shim
---

# Phase 3 — Asset Pipeline Modernization

## Preamble

1. Read `CLAUDE.md` at the repo root first, and `docs/remaster/FINDINGS.md`'s "Asset & resource formats" section.
2. This phase is adjacent to (but distinct from) the save-format binary conventions on the red list — the asset-loading code itself isn't red-listed, but it shares `#pragma pack(1)`-style binary I/O idioms with code that is, so read carefully and be conservative about touching anything that looks shared.
3. **Run the Phase 0 regression harness before and after.**

## The problem

All game assets — sprites (`.SPR`), UI images (`.ICN`/`.COL` pairs), resource archives (`.RES`: fonts, cursors, wave audio), and encyclopedia content (DBF-style `.dbf` files under `data/ENCYC*/`) — are custom 1997 binary formats. No PNG/OGG/modern containers. Offline conversion tooling already exists under `tools/` (`dbfdump`, `delibdb`, `deresx`, `fontdump`, `icnpack`, `paldump`, etc.) but is marked dev-only, not for downstream packaging.

## What to do

1. Decide and confirm with the human which asset category to modernize first — sprites and audio are the most visible to players; don't attempt all categories at once.
2. Extend the existing `tools/` converters (don't reinvent format parsing that already exists there) to produce modern equivalents (e.g. PNG frame sheets for `.SPR`, standard audio containers for the `.RES`-packed wave data).
3. Build a **compatibility shim** so the engine can load either the original legacy format or the converted modern format side-by-side — do not require replacing the original asset packs as a prerequisite. This is important: the game must keep working with unmodified original data files throughout this phase.
4. Be aware that palette-indexed rendering assumptions may be baked into the CPU blit routines (`src/imgfun/x86/` asm fast paths, `src/imgfun/generic/` C fallback) — converting to true-color assets may require touching the blitter too. If so, flag this explicitly as overlapping with rendering-pipeline work (Phase 2 / a future GPU phase) rather than silently expanding this phase's scope.

## Verification before handing back

- Build succeeds; Phase 0 harness passes.
- Original (unconverted) asset packs still load and play correctly — this is the non-negotiable baseline.
- At least one converted asset category verified visually/audibly equivalent to the original by the human (you can do a pixel/waveform diff as a first pass, but final sign-off is a human judgment call, same as the Phase 1 "feel" checks).
- No change to save-game or network-sync formats as a side effect.

## Hand back to the human

Report which asset category you modernized, the conversion pipeline you built or extended, and ask for a side-by-side comparison (original vs. converted) before this is considered done.
