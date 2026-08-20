#!/usr/bin/env bash
#
# Phase 0 regression harness — 7KAA remaster
#
# Runs the game headless (SDL_VIDEODRIVER=dummy) through the built-in
# deterministic test scenario (Battle::run_test(), a fixed-seed 2-nation
# skirmish reached via the `-headless-test-days N` command-line flag added
# for this harness — see include/CmdLine.h / src/CmdLine.cpp / src/OSYS.cpp /
# src/OBATTLE.cpp) and runs TWO separate checks, because they catch two
# different failure modes and one metric cannot do both:
#
#   FAST MODE (-speed 99, primary regression gate)
#     should_next_frame() (src/OSYS.cpp) special-cases config.frame_speed==99
#     to return 1 unconditionally, bypassing the wall-clock pacing gate
#     entirely. HARNESS_WALLTIME_MS in this mode is then ~CPU-bound (measured
#     loop_iters == frame_iters, i.e. zero idle-spin iterations) and moves
#     when the actual simulation cost per frame changes. The gate runs this
#     FAST_RUN_COUNT=5 times per invocation and compares the MEDIAN walltime,
#     not a single sample — see TOLERANCE CALIBRATION below for why, and for
#     the honest (bimodal, not just "noisy") shape of what taking a median
#     here actually buys.
#
#     Why this exists: at the default frame_speed=12, should_next_frame()
#     wall-clock-throttles to 1000/12 ms/frame regardless of how expensive
#     process() actually is. Measured on the reference machine: of a 50-day
#     default-speed run's 45.5s wall time, only ~29ms (0.06%) was spent
#     inside process() — everything else was the gate busy-waiting for
#     real time to pass. A regression would need to make process() ~1500x
#     more expensive before default-speed wall time moved at all. Fast mode
#     removes that dilution.
#
#     Also checks CRC (see below) on every one of the 5 runs — cheap to do
#     in the same runs, and a CRC mismatch between any of the 5 (same fixed
#     seed, should be bit-identical every time) fails immediately as
#     non-determinism, independent of the walltime/tolerance check.
#
#   NORMAL MODE (default speed, pacing-floor check)
#     Runs at the default frame_speed=12 and asserts ONLY that wall time is
#     at least the theoretical pacer floor:
#         floor_ms = days * FRAMES_PER_DAY * 1000 / frame_speed
#                  = days * 10           * 1000 / 12       (defaults)
#     FRAMES_PER_DAY=10 is include/OSYS.h:36; the 1000/frame_speed term is
#     should_next_frame()'s per-frame budget (src/OSYS.cpp). This is a
#     ONE-SIDED lower bound, not a tolerance band: the gate can never
#     legitimately let simulated days pass faster than this rate, so
#     "wall time dropped below the floor" means the pacer/gate mechanism
#     itself broke — exactly the invariant Phase 1e (removing the busy-spin
#     loop) must not violate. It says nothing about simulation cost (fast
#     mode's job) and isn't a two-sided tolerance (there's no fixed upper
#     bound on legitimate slack above the floor — real per-frame cost,
#     OS scheduling, etc. all add to it harmlessly).
#
#   CORRECTNESS (both modes): an 8-bit CRC of all live game objects,
#   computed via CrcStore::record_all(), the SAME aggregate-CRC mechanism
#   the game uses for multiplayer desync detection (src/OCRC_STO.cpp; the
#   per-object crc8()/init_crc() functions it calls live in the red-listed
#   src/OMP_CRC.cpp, which this harness reads but never edits). Only
#   FAST mode's CRC is asserted against the baseline (it's cheap there,
#   and is the designated regression gate); normal mode's CRC is printed
#   for visibility but not asserted — it should always equal fast mode's
#   CRC for the same day count, since -speed only changes wall-clock
#   pacing, never simulation content or order.
#
# NOTE ON CRC STRENGTH: CRC_TYPE (include/CRC.h) is a single byte. That's a
# real, if weak, correctness signal — it's what multiplayer already trusts
# for desync detection, and Phase 0 reuses it rather than inventing a
# stronger check, per the project's "reuse, don't reimplement" constraint.
# It can miss some divergences (~1/256 collision rate on unrelated states).
#
# DIAGNOSTIC-ONLY FIELDS (both modes, printed, never asserted):
#   HARNESS_BUSY_MS     cumulative time inside process() this run
#   HARNESS_FRAME_ITERS frames actually processed
#   HARNESS_LOOP_ITERS  total main_loop iterations (== FRAME_ITERS in fast
#                       mode, since there's no idle-spin there; normal mode's
#                       LOOP_ITERS is ~20000x FRAME_ITERS on the reference
#                       machine — that gap *is* the busy-spin loop's cost)
#   When fast-mode walltime moves, BUSY_MS tells you whether it's genuinely
#   simulation cost that changed vs. iteration/loop-overhead behavior.
#
# KNOWN LANDMINE — do not blindly raise -headless-test-days for fast mode:
#   Per-frame simulation cost in this test scenario is NOT constant over a
#   long run (more units/firms/AI decisions as the 2-nation war develops).
#   Empirically on the reference machine: fast-mode wall time scales
#   ~linearly with day count up to ~200 days (50d=76ms, 100d=152ms,
#   200d=275ms), then goes non-linear somewhere in the 200-300 day range —
#   one observed 300-day fast-mode run pegged a CPU core for minutes with
#   no sign of finishing. Stick to the baselined day count unless you've
#   re-verified a higher one stays fast.
#
# TOLERANCE CALIBRATION (fast mode): the gate takes the MEDIAN of
# FAST_RUN_COUNT=5 back-to-back runs per invocation, not a single sample —
# single-run noise on the reference machine was ~15% of the mean (77, 78,
# 78, 76, 67 ms for one batch of 5 at 50 days). Median-of-5 was expected to
# collapse that noise; what was actually measured is more nuanced and worth
# understanding before trusting the number below:
#
#   12 independent medians (each its own fresh batch of 5 runs, 50 days):
#     78, 67, 68, 67, 68, 67, 67, 77, 67, 67, 67, 67   (mean 68.9, min 67, max 78)
#
#   10 of 12 cluster tightly at 67-68ms (~1.5% spread) -- median-of-5 DOES
#   work as expected most of the time. But 2 of 12 (~17%) landed at 77-78ms
#   instead, each caused by a correlated burst hitting most of that batch's
#   5 individual samples (not one bad sample getting outvoted -- checked the
#   raw per-run values). Root cause not investigated (background system
#   load, CPU frequency scaling, scheduler noise are all plausible; out of
#   scope here). Net effect: median-of-5 reliably kills ORDINARY per-run
#   jitter, but does not fully suppress CORRELATED bursts that affect most
#   of a batch at once -- so the safe tolerance is only modestly tighter
#   than the old single-run estimate (25% vs. the old 30%), not dramatically
#   tighter. 25% covers the observed 78-vs-67 spread (~16%) with margin in
#   both directions regardless of which cluster the baseline capture itself
#   landed in. If you're recalibrating on a different machine: run
#   --fast-only repeatedly (aim for 10+ batches, not 5) and look for this
#   same bimodal pattern before trusting a single batch's spread.
#
# NORMAL-MODE FLOOR MARGIN: the floor is a hard lower bound by construction
# (should_next_frame() cannot let frames through faster than configured),
# so the margin below it only needs to cover measurement quantization
# (misc.get_time() ms resolution), not run-to-run noise — actual normal-mode
# runs measured 45567-45569ms against a 41667ms floor, i.e. ~9.4% *above*,
# never below. Default margin is 2%.
#
# USAGE
#   scripts/phase0_harness.sh                  # build, run both checks
#   scripts/phase0_harness.sh --skip-build      # reuse the existing src/7kaa binary
#   scripts/phase0_harness.sh --fast-only       # just the fast-mode CRC+walltime gate
#   scripts/phase0_harness.sh --pacing-only     # just the normal-mode floor check
#   scripts/phase0_harness.sh --update-baseline # run both once and overwrite the baseline
#   scripts/phase0_harness.sh --days N          # override day count (ad-hoc; see landmine above)
#   scripts/phase0_harness.sh --tolerance PCT   # override fast-mode tolerance (default 25)
#   scripts/phase0_harness.sh --floor-margin PCT  # override normal-mode floor margin (default 2)
#
# REGENERATING THE BASELINE
#   The baseline (scripts/phase0_baseline.txt) should change RARELY and only
#   as an explicit, deliberate action — never as an accidental side effect.
#   Regenerate it only when:
#     - You've made a change you *know* legitimately alters simulation
#       state or timing (e.g. a change explicitly toggled to new behavior
#       for testing), AND
#     - You've reviewed the new CRC/timing and are confident it reflects
#       that intended change, not a bug.
#   To regenerate: scripts/phase0_harness.sh --update-baseline
#   This overwrites scripts/phase0_baseline.txt — review the diff before
#   committing it, and explain why it changed in the commit message.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="$REPO_ROOT/src/7kaa"
DATA_DIR="$REPO_ROOT/data"
BASELINE_FILE="$REPO_ROOT/scripts/phase0_baseline.txt"

DEFAULT_DAYS=50
DEFAULT_FAST_TOLERANCE_PCT=25
DEFAULT_FLOOR_MARGIN_PCT=2
FRAMES_PER_DAY=10        # include/OSYS.h — keep in sync
DEFAULT_FRAME_SPEED=12   # src/OCONFIG.cpp Config::init() — keep in sync

DAYS=""
FAST_TOLERANCE_PCT="$DEFAULT_FAST_TOLERANCE_PCT"
FLOOR_MARGIN_PCT="$DEFAULT_FLOOR_MARGIN_PCT"
DO_BUILD=1
RUN_FAST=1
RUN_NORMAL=1
MODE="compare"   # compare | update-baseline

while [ $# -gt 0 ]; do
	case "$1" in
		--skip-build)
			DO_BUILD=0
			shift
			;;
		--fast-only)
			RUN_NORMAL=0
			shift
			;;
		--pacing-only)
			RUN_FAST=0
			shift
			;;
		--update-baseline)
			MODE="update-baseline"
			shift
			;;
		--days)
			DAYS="$2"
			shift 2
			;;
		--tolerance)
			FAST_TOLERANCE_PCT="$2"
			shift 2
			;;
		--floor-margin)
			FLOOR_MARGIN_PCT="$2"
			shift 2
			;;
		-h|--help)
			sed -n '2,145p' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "Unknown argument: $1" >&2
			exit 2
			;;
	esac
done

if [ "$MODE" = "update-baseline" ]; then
	DAYS="${DAYS:-$DEFAULT_DAYS}"
elif [ -f "$BASELINE_FILE" ]; then
	BASELINE_DAYS="$(grep '^HARNESS_DAYS=' "$BASELINE_FILE" | cut -d= -f2)"
	DAYS="${DAYS:-$BASELINE_DAYS}"
else
	DAYS="${DAYS:-$DEFAULT_DAYS}"
fi

if [ "$DO_BUILD" = "1" ]; then
	echo "== Building ($REPO_ROOT) ==" >&2
	make -C "$REPO_ROOT" -j"$(nproc)" >/dev/null
fi

if [ ! -x "$BINARY" ]; then
	echo "ERROR: $BINARY not found or not executable. Build first (omit --skip-build)." >&2
	exit 1
fi

if [ ! -d "$DATA_DIR" ]; then
	echo "ERROR: game data dir not found at $DATA_DIR" >&2
	exit 1
fi

# run_harness <extra binary args...> -> sets RUN_DAYS/RUN_CRC/RUN_WALLTIME_MS/
# RUN_BUSY_MS/RUN_FRAME_ITERS/RUN_LOOP_ITERS globals from one headless run.
run_harness() {
	local scratch_config
	scratch_config="$(mktemp -d)"
	local output
	output="$(cd "$REPO_ROOT/src" && \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		SKDATA="$DATA_DIR" SKCONFIG="$scratch_config" \
		timeout 300 "$BINARY" -noaudio "$@" -headless-test-days "$DAYS" 2>&1)" || {
		echo "ERROR: harness run failed or timed out. Full output:" >&2
		echo "$output" >&2
		rm -rf "$scratch_config"
		exit 1
	}
	rm -rf "$scratch_config"

	RUN_DAYS="$(echo "$output" | grep '^HARNESS_DAYS=' | cut -d= -f2)"
	RUN_CRC="$(echo "$output" | grep '^HARNESS_CRC=' | cut -d= -f2)"
	RUN_WALLTIME_MS="$(echo "$output" | grep '^HARNESS_WALLTIME_MS=' | cut -d= -f2)"
	RUN_BUSY_MS="$(echo "$output" | grep '^HARNESS_BUSY_MS=' | cut -d= -f2)"
	RUN_FRAME_ITERS="$(echo "$output" | grep '^HARNESS_FRAME_ITERS=' | cut -d= -f2)"
	RUN_LOOP_ITERS="$(echo "$output" | grep '^HARNESS_LOOP_ITERS=' | cut -d= -f2)"

	if [ -z "$RUN_CRC" ] || [ -z "$RUN_WALLTIME_MS" ]; then
		echo "ERROR: harness output missing expected HARNESS_* fields. Full output:" >&2
		echo "$output" >&2
		exit 1
	fi
}

# median_of <values...> -> prints the median (FAST_RUN_COUNT is odd, so this
# is always a real sample, never an average of two).
median_of() {
	local sorted
	sorted=($(printf '%s\n' "$@" | sort -n))
	local mid=$(( (${#sorted[@]} - 1) / 2 ))
	echo "${sorted[$mid]}"
}

FAIL=0
FAST_RUN_COUNT=5

FAST_CRC="" FAST_WALLTIME_MS="" FAST_BUSY_MS="" FAST_FRAME_ITERS="" FAST_LOOP_ITERS=""
if [ "$RUN_FAST" = "1" ]; then
	echo "== [fast]   running $FAST_RUN_COUNT x: $DAYS in-game days, -speed 99 ==" >&2
	fast_crcs=() fast_walltimes=() fast_busy=()
	for i in $(seq 1 "$FAST_RUN_COUNT"); do
		run_harness -speed 99
		fast_crcs+=("$RUN_CRC")
		fast_walltimes+=("$RUN_WALLTIME_MS")
		fast_busy+=("$RUN_BUSY_MS")
		echo "   [fast run $i/$FAST_RUN_COUNT] crc=$RUN_CRC wall_ms=$RUN_WALLTIME_MS busy_ms=$RUN_BUSY_MS" >&2
	done
	FAST_FRAME_ITERS="$RUN_FRAME_ITERS"    # deterministic frame count, same every run
	FAST_LOOP_ITERS="$RUN_LOOP_ITERS"

	# All 5 runs are the same fixed-seed scenario -- a CRC mismatch between
	# any of them is non-determinism, not noise. Fail loudly on it rather
	# than silently taking whichever CRC the median walltime happened to
	# come from.
	FAST_CRC="${fast_crcs[0]}"
	for c in "${fast_crcs[@]}"; do
		if [ "$c" != "$FAST_CRC" ]; then
			echo "FAIL: [fast] CRC differs across repeated runs of the same fixed-seed scenario: ${fast_crcs[*]} -- this is non-determinism, not noise" >&2
			FAIL=1
			break
		fi
	done

	FAST_WALLTIME_MS="$(median_of "${fast_walltimes[@]}")"
	FAST_BUSY_MS="$(median_of "${fast_busy[@]}")"
	echo "-- [fast]   days=$DAYS crc=$FAST_CRC median_wall_ms=$FAST_WALLTIME_MS (samples: ${fast_walltimes[*]}) median_busy_ms=$FAST_BUSY_MS frame_iters=$FAST_FRAME_ITERS loop_iters=$FAST_LOOP_ITERS --" >&2
fi

NORMAL_CRC="" NORMAL_WALLTIME_MS="" NORMAL_BUSY_MS="" NORMAL_FRAME_ITERS="" NORMAL_LOOP_ITERS=""
if [ "$RUN_NORMAL" = "1" ]; then
	echo "== [normal] running: $DAYS in-game days, default speed ==" >&2
	run_harness
	NORMAL_CRC="$RUN_CRC"
	NORMAL_WALLTIME_MS="$RUN_WALLTIME_MS"
	NORMAL_BUSY_MS="$RUN_BUSY_MS"
	NORMAL_FRAME_ITERS="$RUN_FRAME_ITERS"
	NORMAL_LOOP_ITERS="$RUN_LOOP_ITERS"
	echo "-- [normal] days=$RUN_DAYS crc=$NORMAL_CRC wall_ms=$NORMAL_WALLTIME_MS busy_ms=$NORMAL_BUSY_MS frame_iters=$NORMAL_FRAME_ITERS loop_iters=$NORMAL_LOOP_ITERS --" >&2
fi

if [ "$MODE" = "update-baseline" ]; then
	{
		echo "# Phase 0 harness baseline — regenerate only deliberately, see script header."
		echo "# Captured: $(date -u +%Y-%m-%dT%H:%M:%SZ) at commit $(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
		echo "HARNESS_DAYS=$DAYS"
		echo "FAST_CRC=$FAST_CRC"
		echo "FAST_WALLTIME_MS=$FAST_WALLTIME_MS"
		echo "FAST_TOLERANCE_PCT=$FAST_TOLERANCE_PCT"
		echo "FLOOR_MARGIN_PCT=$FLOOR_MARGIN_PCT"
		echo "# informational only, not asserted on comparison runs:"
		echo "# NORMAL_CRC=$NORMAL_CRC (should equal FAST_CRC)"
		echo "# NORMAL_WALLTIME_MS=$NORMAL_WALLTIME_MS"
	} > "$BASELINE_FILE"
	echo "Baseline written to $BASELINE_FILE" >&2
	exit 0
fi

if [ ! -f "$BASELINE_FILE" ]; then
	echo "ERROR: no baseline file at $BASELINE_FILE. Run with --update-baseline first." >&2
	exit 1
fi

BASELINE_HARNESS_DAYS="$(grep '^HARNESS_DAYS=' "$BASELINE_FILE" | cut -d= -f2)"
BASELINE_FAST_CRC="$(grep '^FAST_CRC=' "$BASELINE_FILE" | cut -d= -f2)"
BASELINE_FAST_WALLTIME_MS="$(grep '^FAST_WALLTIME_MS=' "$BASELINE_FILE" | cut -d= -f2)"
BASELINE_FAST_TOLERANCE="$(grep '^FAST_TOLERANCE_PCT=' "$BASELINE_FILE" | cut -d= -f2 || true)"
FAST_TOLERANCE_PCT="${BASELINE_FAST_TOLERANCE:-$FAST_TOLERANCE_PCT}"
BASELINE_FLOOR_MARGIN="$(grep '^FLOOR_MARGIN_PCT=' "$BASELINE_FILE" | cut -d= -f2 || true)"
FLOOR_MARGIN_PCT="${BASELINE_FLOOR_MARGIN:-$FLOOR_MARGIN_PCT}"

if [ "$RUN_FAST" = "1" ]; then
	if [ "$DAYS" != "$BASELINE_HARNESS_DAYS" ]; then
		echo "WARNING: ran $DAYS days but baseline was captured over a different day count; fast-mode comparison may be meaningless." >&2
	fi
	if [ "$FAST_CRC" != "$BASELINE_FAST_CRC" ]; then
		echo "FAIL: [fast] CRC mismatch. baseline=$BASELINE_FAST_CRC got=$FAST_CRC" >&2
		FAIL=1
	else
		echo "PASS: [fast] CRC matches baseline ($FAST_CRC)" >&2
	fi

	LOW=$(( BASELINE_FAST_WALLTIME_MS * (100 - FAST_TOLERANCE_PCT) / 100 ))
	HIGH=$(( BASELINE_FAST_WALLTIME_MS * (100 + FAST_TOLERANCE_PCT) / 100 ))
	if [ "$FAST_WALLTIME_MS" -lt "$LOW" ] || [ "$FAST_WALLTIME_MS" -gt "$HIGH" ]; then
		echo "FAIL: [fast] wall-clock time $FAST_WALLTIME_MS ms outside ±${FAST_TOLERANCE_PCT}% of baseline $BASELINE_FAST_WALLTIME_MS ms (expected [$LOW, $HIGH])" >&2
		FAIL=1
	else
		echo "PASS: [fast] wall-clock time $FAST_WALLTIME_MS ms within ±${FAST_TOLERANCE_PCT}% of baseline $BASELINE_FAST_WALLTIME_MS ms" >&2
	fi
fi

if [ "$RUN_NORMAL" = "1" ]; then
	# floor_ms = days * FRAMES_PER_DAY * 1000 / frame_speed (see header derivation)
	FLOOR_MS=$(( DAYS * FRAMES_PER_DAY * 1000 / DEFAULT_FRAME_SPEED ))
	FLOOR_MIN=$(( FLOOR_MS * (100 - FLOOR_MARGIN_PCT) / 100 ))
	if [ "$NORMAL_WALLTIME_MS" -lt "$FLOOR_MIN" ]; then
		echo "FAIL: [normal] wall-clock time $NORMAL_WALLTIME_MS ms is below the pacing floor $FLOOR_MS ms (-${FLOOR_MARGIN_PCT}% margin = $FLOOR_MIN ms) — the frame-rate gate let simulated days pass faster than configured, which should be impossible" >&2
		FAIL=1
	else
		echo "PASS: [normal] wall-clock time $NORMAL_WALLTIME_MS ms is at/above the pacing floor $FLOOR_MS ms (-${FLOOR_MARGIN_PCT}% margin = $FLOOR_MIN ms)" >&2
	fi
fi

exit $FAIL
