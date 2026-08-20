---
description: 7KAA remaster Phase 4 — fix known AI dead code/bugs and evolve difficulty away from cheat-income, each change behind its own toggle
---

# Phase 4 — AI Overhaul

## Preamble

1. Read `CLAUDE.md` at the repo root first, and `docs/remaster/FINDINGS.md`'s "AI Subsystem Findings" section in full.
2. **This phase touches red-listed files** (`src/OAI_*.cpp`, `src/OTOWNAI.cpp`, `src/OUNITAI.cpp`, `src/OFIRMAI.cpp`, `src/OSPATH.cpp`, `src/ONATIONB.cpp`). The `PreToolUse` hook (`scripts/hooks/guard-red-list.sh`) will block edits to these by default. This is intentional — get explicit human sign-off before starting, and have the human set `SEVENKAA_REDLIST_ACK` in the **launch environment** (before starting this Claude Code session) listing the specific files you're authorized to edit for this session. Do not attempt to set this variable yourself from inside the session — it should not work if you try, and if it does, stop and report that as a serious finding (see `scripts/hooks/guard-red-list.sh`'s own documentation for why).
3. AI decisions feed CRC-synced state (they drive unit orders, which are part of multiplayer lockstep sync). **Every change in this phase must ship behind its own named toggle** (`ConfigAdv`-style, defaulting to legacy behavior) — this is not optional here the way it was optional-but-recommended in earlier phases.
4. **Run the Phase 0 regression harness before and after every individual sub-change**, not just once at the end of the whole phase — with AI logic, it's easy for a change's effect to only show up several in-game days later.

## Scope — treat each of these as an independently toggle-gated sub-change, not one big commit

1. **`src/OAI_MILI.cpp :: Nation::think_close_camp()`** — currently stubbed to `return 0` immediately; the AI never voluntarily closes an inefficient military camp. Implement real logic behind a toggle (e.g. `ai_close_inefficient_camp`).
2. **`src/OAI_MAIN.cpp :: Nation::think_explore()`** — currently an empty stub despite being wired into the AI's think rotation. Implement real exploration behavior behind its own toggle.
3. **Pathfinding heuristic** (`src/OSPATH.cpp`, the `SeekPath` search) — currently squared Euclidean distance compared against a linear step cost (author's own comment: "should really use sqrt()"), making it non-admissible. Consider fixing to a proper admissible heuristic behind a toggle — but note this could change which paths units take, which is exactly the kind of visible behavior change that needs careful playtesting, not just a CRC pass.
4. **`src/OSPATH.cpp :: SeekPath::result_node_distance(ResultNode*, ResultNode*)`** — re-verify by hand whether the X/Y-axis mixup described in `docs/remaster/FINDINGS.md` is actually present (the earlier research pass flagged this but it was explicitly marked as needing a fresh read before this phase touches the file). If confirmed, fix behind a toggle.
5. **Cheat-income difficulty model** (`src/ONATIONB.cpp :: NationBase::add_cheat()`) — this is the biggest design decision in this phase, not just a bugfix. Do not silently remove or rework this without an explicit conversation with the human first: reducing/removing cheat income without compensating AI logic improvements could make higher difficulties trivially easy, which would be a real regression in what long-time players experience. Bring options (e.g. reduce cheat reliance gradually as other AI improvements land, vs. keep cheat income but make it less central) to the human rather than deciding unilaterally.

## What "toggle-gated" means here, concretely

Follow the existing precedent exactly: `include/ConfigAdv.h :: fix_path_blocked_by_team` — a named boolean, defaulted to legacy/off behavior, parsed from `config.txt`, checked at the specific call site(s) the fix affects. New saves/games can opt into the new behavior; existing saves/replays keep the old behavior by default unless the human decides otherwise for a specific change.

## Verification before handing back (per sub-change, not just once at the end)

- Build succeeds.
- Phase 0 harness: run with the toggle in its default (legacy) state — CRC and pacing should be unchanged from pre-change baseline. Run again with the toggle enabled — CRC will very likely differ (that's expected and fine, since AI behavior actually changed), but confirm it's *deterministic* across repeated runs with the toggle enabled (same seed + same toggle state → same CRC every time) — that's the property that actually matters for multiplayer sync once the new behavior is used.
- A written note on expected balance impact for each change — even a brief one — since these are exactly the kind of change that can quietly make the game harder or easier in a way that violates "keep the core gameplay."

## Hand back to the human

For each sub-change: report the toggle name, default state, what changed, the balance-impact note, and the CRC-determinism confirmation. This phase absolutely requires human playtesting before any toggle's default is flipped from legacy to new behavior — do not flip a default yourself.
