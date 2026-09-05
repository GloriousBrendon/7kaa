#!/usr/bin/env bash
#
# Phase 4 AI harness — 7KAA remaster
#
# Runs the all-AI economy scenario (Battle::run_ai_test(), reached via the
# -headless-ai-days flag) headless and checks three things. This is ADDITIVE to
# scripts/phase0_harness.sh and shares nothing with it: that script, its
# scenario (Battle::run_test()) and scripts/phase0_baseline.txt are the
# project's determinism anchor and are never touched from here.
#
# WHY A SECOND HARNESS EXISTS AT ALL
#
#   Phase 0's scenario spawns soldiers with no king and no town, so it has no
#   economy, and NationBase::next_day() -> check_lose() ends it on day 263. It
#   proves determinism, which is what it was built for. But it exercises almost
#   none of what Phase 4 changes: with no town and no firm, the AI's economic
#   think categories (think_build_firm, think_trading, think_town) have nothing
#   to act on, and think_close_camp() has no camp to close. Measuring an AI
#   change against it would measure nothing.
#
#   This scenario goes through Battle::create_pregame_object() instead, so each
#   nation starts with the town, camp, king and units a real game gives it. It
#   is all-AI: no NATION_OWN nation is created, so nation_array.player_recno
#   stays 0.
#
# THE THREE SIGNALS, AND WHY IT TAKES THREE
#
#   (a) PER-DAY CRC SERIES  (AIH_D <day> <crc>)          -- the guard
#       CrcStore::record_all() once per in-game day: the same aggregate the
#       game uses for multiplayer desync detection. Answers "did this change
#       alter the simulation at all, and from exactly which day". With a Phase 4
#       toggle in its legacy default this series MUST be identical to the
#       pre-change baseline -- a much stronger no-op proof than a single
#       end-of-run CRC, which can hide a divergence that happened and washed
#       out. With the toggle on, the first differing day is the breakpoint.
#       Limitation: CRC_TYPE is one byte, so it says WHEN, never WHAT, and
#       misses ~1/256 of divergences.
#
#   (b) PER-DAY PER-NATION METRICS  (AIH_N <day> <nation> ...)  -- the verdict
#       Load-bearing, not a nice-to-have. CrcStore hashes only the NationBase
#       subobject (src/OMP_CRC.cpp :: NationBase::crc8 memcpy's sizeof
#       (NationBase)); every AI field -- pref_*, ai_*_count, action_array,
#       attack_camp_array -- lives in the derived Nation and is INVISIBLE to
#       signal (a). So (a) can tell you the AI decided differently but never
#       what it decided or whether that was the intent. (b) is where you see
#       direction: camps opened and closed, firms built, cash and food curves,
#       who went to war with whom and when.
#
#   (c) END-OF-RUN SUMMARY  (AIH_SUMMARY_BEGIN .. AIH_SUMMARY_END)
#       Final state per nation plus a firm-type breakdown. This is what goes in
#       a commit message.
#
#   All three are produced by src/OAIHARN.cpp reading NationBase, firm_res and
#   CrcStore after the fact. NO OAI_* FILE WAS CHANGED to support any of it, and
#   nothing in the AI knows the harness exists -- instrumenting the code under
#   test is exactly what this design avoids.
#
# WHAT THIS SCRIPT ASSERTS
#
#   1. DETERMINISM.  REPEATS (default 3) back-to-back runs of the pinned seed
#      must produce byte-identical CRC series AND byte-identical metric series.
#      This is the instrument, not the code read: CrcStore::record_firms() and
#      record_towns() contribute nothing but a size field under Phase 0's
#      scenario (it creates zero firms and zero towns), so this harness is the
#      first thing in the project to exercise Firm::init_crc and Town::crc8 for
#      real. Inspection says they are pointer-free and zero-initialised; this
#      check is what actually proves it.
#
#   2. BASELINE MATCH.  CRC series equals the stored series line for line, and
#      the metric series digest matches. On failure the script names the FIRST
#      differing day rather than just saying "differs".
#
#   3. PINNED KNOBS.  Every AIH_* header field must equal the baseline's. The
#      scenario pins its own Config in Battle::init_ai_test_config() rather than
#      inheriting, and records what it pinned; this asserts the recording still
#      says what the baseline was captured under. A knob that drifts silently
#      would otherwise invalidate every number here without failing anything.
#
#   4. CONFIG ISOLATION.  Each run gets a fresh scratch $SKCONFIG with an EMPTY
#      config.txt planted in it, and AIH_CONFIG_PATH is asserted to be that
#      file. Both halves are load-bearing and the trap is live on this machine:
#      ConfigAdv::load() falls back to a bare relative "config.txt" that
#      resolves inside $SKDATA (Sys::chdir_to_game_dir() has already chdir'd
#      there), and an untracked data/config.txt exists. A run that reached it
#      would silently measure someone's personal settings. The dir is also
#      fresh per run because Config::deinit() writes CONFIG.DAT back into
#      $SKCONFIG on exit -- a re-used dir would feed one run's settings into
#      the next.
#
#   5. NO EARLY GAME-OVER.  AIH_GAME_OVER=1 before the requested day count is a
#      HARD FAILURE here. This is the opposite of Phase 0, where the day-263
#      stop is the documented and expected end of that scenario. Here it means
#      the game resolved (one nation left standing --
#      goal_destroy_nation_achieved() is the only win condition still armed) and
#      every day after that point is missing from the series.
#
#   6. SPEED INVARIANCE.  The same scenario run at -speed 99 (which bypasses
#      Sys::should_next_frame()'s pacing gate entirely) and at a paced speed
#      must produce identical CRC and metric series. Sys::process() wraps
#      disp_frame() in misc.lock_seed()/unlock_seed(), so rendering cannot
#      consume RNG draws -- this asserts that property over a long economy run
#      rather than assuming it. Phase 0 prints its normal-mode CRC but does not
#      assert it; this does. It is the slow check (the paced run really does
#      wait), so it uses its own smaller day count.
#
# WHAT THIS SCRIPT DELIBERATELY DOES NOT ASSERT
#
#   Wall-clock time. Phase 0 owns pacing regression via its fast-mode tolerance
#   band and its normal-mode pacing floor. Timings are printed here as
#   diagnostics only -- a second, differently-calibrated timing gate would just
#   be a second thing to recalibrate.
#
# WHAT THIS SCENARIO CANNOT MEASURE
#
#   The AI's bias toward attacking the human player. src/OAI_ATTK.cpp adds a
#   flat rating bonus to attack-target scoring `if (!nationPtr->is_ai())`,
#   scaled by config.ai_aggressiveness. With no human nation in the game that
#   branch never fires, and there is no way to make it fire without putting a
#   passive NATION_OWN nation in as prey, which would distort the very game
#   being measured. That bias is playtest-verified, not harness-verified. See
#   the note in scripts/phase4_ai_baseline.txt.
#
# USAGE
#   scripts/phase4_ai_harness.sh                  # build, run every check
#   scripts/phase4_ai_harness.sh --skip-build     # reuse the existing binary
#   scripts/phase4_ai_harness.sh --fast-only      # skip the slow speed-invariance check
#   scripts/phase4_ai_harness.sh --speed-only     # only the speed-invariance check
#   scripts/phase4_ai_harness.sh --update-baseline
#   scripts/phase4_ai_harness.sh --days N         # ad-hoc day count (baseline comparison is skipped)
#   scripts/phase4_ai_harness.sh --repeats N      # determinism repeats (default 3)
#   scripts/phase4_ai_harness.sh --dump DIR       # keep each run's full output in DIR
#
# REGENERATING THE BASELINE
#   Only as a deliberate act, and only when you know the simulation legitimately
#   changed -- e.g. you enabled a Phase 4 toggle on purpose and reviewed what it
#   did. Review the diff before committing it and say in the commit message why
#   it moved. A baseline quietly updated to match an unreviewed result is worse
#   than no baseline.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="$REPO_ROOT/src/7kaa"
DATA_DIR="$REPO_ROOT/data"
BASELINE_FILE="$REPO_ROOT/scripts/phase4_ai_baseline.txt"

# 2000 days: the pinned seed's game runs to day 15411 before one nation is left
# standing, so this sits at ~13% of the natural length with a wide margin. It is
# also past the first nation elimination (~day 1500 at this seed), so the
# capture/attack AI is genuinely exercised rather than just the economy. At the
# pinned OPTION_HIGH aggressiveness the think dispatcher's interval is 15 days,
# so each of the twelve think categories fires ~133 times in a run.
DEFAULT_DAYS=2000

# The speed-invariance check pays real wall-clock time by construction, so it
# runs over fewer days. 300 days at -speed 60 is ~50s; the same 300 days at
# -speed 99 is ~0.3s.
DEFAULT_SPEED_DAYS=300
DEFAULT_PACED_SPEED=60

DEFAULT_REPEATS=3

DAYS=""
SPEED_DAYS="$DEFAULT_SPEED_DAYS"
PACED_SPEED="$DEFAULT_PACED_SPEED"
REPEATS="$DEFAULT_REPEATS"
DO_BUILD=1
RUN_MAIN=1
RUN_SPEED=1
MODE="compare"
DUMP_DIR=""
DAYS_OVERRIDDEN=0

while [ $# -gt 0 ]; do
	case "$1" in
		--skip-build)     DO_BUILD=0; shift ;;
		--fast-only)      RUN_SPEED=0; shift ;;
		--speed-only)     RUN_MAIN=0; shift ;;
		--update-baseline) MODE="update-baseline"; shift ;;
		--days)           DAYS="$2"; DAYS_OVERRIDDEN=1; shift 2 ;;
		--repeats)        REPEATS="$2"; shift 2 ;;
		--speed-days)     SPEED_DAYS="$2"; shift 2 ;;
		--paced-speed)    PACED_SPEED="$2"; shift 2 ;;
		--dump)           DUMP_DIR="$2"; shift 2 ;;
		-h|--help)        sed -n '2,150p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; exit 2 ;;
	esac
done

if [ "$MODE" = "update-baseline" ]; then
	DAYS="${DAYS:-$DEFAULT_DAYS}"
elif [ -f "$BASELINE_FILE" ]; then
	BASELINE_DAYS="$(grep '^AIH_DAYS=' "$BASELINE_FILE" | head -1 | cut -d= -f2)"
	DAYS="${DAYS:-$BASELINE_DAYS}"
else
	DAYS="${DAYS:-$DEFAULT_DAYS}"
fi

if [ "$DO_BUILD" = "1" ]; then
	echo "== Building ($REPO_ROOT) ==" >&2
	make -C "$REPO_ROOT" -j"$(nproc)" >/dev/null
fi

[ -x "$BINARY" ] || { echo "ERROR: $BINARY not found or not executable." >&2; exit 1; }
[ -d "$DATA_DIR" ] || { echo "ERROR: game data dir not found at $DATA_DIR" >&2; exit 1; }

if [ -n "$DUMP_DIR" ]; then
	mkdir -p "$DUMP_DIR"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAIL=0

# The header fields that describe the scenario rather than this particular
# invocation. AIH_CONFIG_PATH is a fresh temp path per run, AIH_DAYS_REQUESTED
# follows --days, and AIH_FRAME_SPEED follows -speed, so none of those three can
# be compared against a stored value.
header_knobs() {
	sed -n '/^AIH_VERSION=/,/^AIH_HEADER_END=/p' "$1" \
		| grep -Ev '^(AIH_CONFIG_PATH|AIH_DAYS_REQUESTED|AIH_FRAME_SPEED)='
}

# run_scenario <outfile> <days> <extra binary args...>
#
# Fresh $SKCONFIG per run with an empty config.txt planted in it, then assert the
# binary actually loaded that file. See CONFIG ISOLATION in the header for why
# both halves are needed and why the dir cannot be re-used.
run_scenario() {
	local outfile="$1"; shift
	local days="$1"; shift

	local scratch expected
	scratch="$(mktemp -d)"
	expected="$scratch/config.txt"
	: > "$expected"

	if ! ( cd "$REPO_ROOT/src" && \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		SKDATA="$DATA_DIR" SKCONFIG="$scratch" \
		timeout 1800 "$BINARY" -noaudio "$@" -headless-ai-days "$days" ) > "$outfile" 2>&1
	then
		echo "ERROR: scenario run failed or timed out. Tail of output:" >&2
		tail -40 "$outfile" >&2
		rm -rf "$scratch"
		exit 1
	fi
	rm -rf "$scratch"

	local got
	got="$(grep '^AIH_CONFIG_PATH=' "$outfile" | cut -d= -f2- || true)"
	if [ "$got" != "$expected" ]; then
		echo "ERROR: config isolation broken -- this run loaded config.txt from" >&2
		echo "         ${got:-<binary printed no AIH_CONFIG_PATH>}" >&2
		echo "       but the harness pinned" >&2
		echo "         $expected" >&2
		echo "       A bare relative path means ConfigAdv::load() fell through into" >&2
		echo "       the game data dir and picked up an untracked personal" >&2
		echo "       config.txt. Refusing to report a measurement taken under" >&2
		echo "       unknown settings." >&2
		exit 1
	fi

	if ! grep -q '^AIH_HEADER_END=1' "$outfile"; then
		echo "ERROR: run produced no AI harness header. Tail of output:" >&2
		tail -40 "$outfile" >&2
		exit 1
	fi
}

# assert_run_completed <outfile> <label> <requested days>
#
# An early game-over is a hard failure in this mode. Phase 0 tolerates one
# because day 263 is that scenario's real end; here it means the game resolved
# and the rest of the series does not exist.
assert_run_completed() {
	local outfile="$1" label="$2" want="$3"
	local gameOver gotDays alive
	gameOver="$(grep '^AIH_GAME_OVER=' "$outfile" | cut -d= -f2)"
	gotDays="$(grep '^AIH_DAYS=' "$outfile" | tail -1 | cut -d= -f2)"
	alive="$(grep '^AIH_NATIONS_ALIVE=' "$outfile" | cut -d= -f2)"

	if [ "$gameOver" != "0" ]; then
		echo "FAIL: [$label] scenario ended early at day $gotDays of $want requested" >&2
		echo "      (AIH_GAME_OVER=1, nations alive=$alive). The game resolved before" >&2
		echo "      the run finished -- goal_destroy_nation_achieved() fires when only" >&2
		echo "      one nation is left. Every day past $gotDays is missing from the" >&2
		echo "      series, so the measurement is incomplete, not merely short." >&2
		echo "      Lower the day count or re-pin the scenario; do not baseline this." >&2
		FAIL=1
		return 1
	fi
	if [ "$gotDays" != "$want" ]; then
		echo "FAIL: [$label] ran $gotDays days but $want were requested" >&2
		FAIL=1
		return 1
	fi
	return 0
}

# first_crc_divergence <fileA> <fileB> -- prints "day <n>: <a> vs <b>", or nothing
first_crc_divergence() {
	diff <(grep '^AIH_D ' "$1") <(grep '^AIH_D ' "$2") \
		| grep '^<' | head -1 | awk '{printf "day %s (this run: crc %s)", $3, $4}'
}

#-------------------- main determinism + baseline check --------------------#

MAIN_LOGS=()
if [ "$RUN_MAIN" = "1" ]; then
	echo "== [ai] $REPEATS x $DAYS in-game days, -speed 99 ==" >&2
	for i in $(seq 1 "$REPEATS"); do
		log="$WORK/run$i.log"
		s=$(date +%s%3N)
		run_scenario "$log" "$DAYS" -speed 99
		e=$(date +%s%3N)
		MAIN_LOGS+=("$log")
		assert_run_completed "$log" "ai run $i" "$DAYS" || true
		printf '   [run %d/%d] wall_ms=%s days=%s final_crc=%s nations_alive=%s firms=%s\n' \
			"$i" "$REPEATS" "$((e-s))" \
			"$(grep '^AIH_DAYS=' "$log" | tail -1 | cut -d= -f2)" \
			"$(grep '^AIH_FINAL_CRC=' "$log" | cut -d= -f2)" \
			"$(grep '^AIH_NATIONS_ALIVE=' "$log" | cut -d= -f2)" \
			"$(grep '^AIH_WORLD_FIRMS=' "$log" | cut -d= -f2)" >&2
		if [ -n "$DUMP_DIR" ]; then
			cp "$log" "$DUMP_DIR/ai_run$i.log"
		fi
	done

	# 1. determinism across repeats -- the instrument for the firm/town CRC
	# paths this scenario exercises for the first time.
	REF="${MAIN_LOGS[0]}"
	det_ok=1
	for log in "${MAIN_LOGS[@]:1}"; do
		if ! diff -q <(grep '^AIH_D ' "$REF") <(grep '^AIH_D ' "$log") >/dev/null; then
			echo "FAIL: [ai] per-day CRC series differs between repeated runs of the same" >&2
			echo "      fixed-seed scenario -- this is non-determinism, not noise." >&2
			echo "      First divergence: $(first_crc_divergence "$log" "$REF")" >&2
			det_ok=0; FAIL=1
		fi
		if ! diff -q <(grep '^AIH_N ' "$REF") <(grep '^AIH_N ' "$log") >/dev/null; then
			echo "FAIL: [ai] per-day metric series differs between repeated runs." >&2
			det_ok=0; FAIL=1
		fi
	done
	[ "$det_ok" = "1" ] && echo "PASS: [ai] $REPEATS runs produced byte-identical CRC and metric series" >&2

	grep '^AIH_D ' "$REF" > "$WORK/crc_series.txt"
	grep '^AIH_N ' "$REF" > "$WORK/metric_series.txt"
	METRIC_SHA="$(sha256sum "$WORK/metric_series.txt" | cut -d' ' -f1)"
	header_knobs "$REF" > "$WORK/knobs.txt"
fi

#-------------------------- speed-invariance check -------------------------#

if [ "$RUN_SPEED" = "1" ]; then
	echo "== [speed] $SPEED_DAYS days at -speed 99 vs -speed $PACED_SPEED ==" >&2
	run_scenario "$WORK/fast.log" "$SPEED_DAYS" -speed 99
	assert_run_completed "$WORK/fast.log" "speed fast" "$SPEED_DAYS" || true
	run_scenario "$WORK/paced.log" "$SPEED_DAYS" -speed "$PACED_SPEED"
	assert_run_completed "$WORK/paced.log" "speed paced" "$SPEED_DAYS" || true

	if diff -q <(grep '^AIH_D ' "$WORK/fast.log") <(grep '^AIH_D ' "$WORK/paced.log") >/dev/null \
	   && diff -q <(grep '^AIH_N ' "$WORK/fast.log") <(grep '^AIH_N ' "$WORK/paced.log") >/dev/null
	then
		echo "PASS: [speed] -speed 99 and -speed $PACED_SPEED produced identical CRC and metric series over $SPEED_DAYS days" >&2
	else
		echo "FAIL: [speed] frame speed changed the simulation. Sys::process() locks the" >&2
		echo "      random seed around disp_frame(), so pacing and rendering must not be" >&2
		echo "      able to affect simulation content. First CRC divergence:" >&2
		echo "      $(first_crc_divergence "$WORK/paced.log" "$WORK/fast.log")" >&2
		FAIL=1
	fi
fi

#------------------------------ baseline I/O -------------------------------#

if [ "$MODE" = "update-baseline" ]; then
	if [ "$RUN_MAIN" != "1" ]; then
		echo "ERROR: --update-baseline needs the main run; drop --speed-only." >&2
		exit 1
	fi
	{
		echo "# Phase 4 AI harness baseline — regenerate only deliberately, see"
		echo "# scripts/phase4_ai_harness.sh. This file is NOT the Phase 0 determinism"
		echo "# anchor; scripts/phase0_baseline.txt is, and it is untouched by this."
		echo "#"
		echo "# NOT MEASURED HERE: the AI's human-targeting bias. src/OAI_ATTK.cpp biases"
		echo "# attack-target scoring with 'if (!nationPtr->is_ai())', scaled by"
		echo "# ai_aggressiveness. This scenario is all-AI (AIH_PLAYER_RECNO=0), so that"
		echo "# branch never fires. It cannot be made to fire without adding a passive"
		echo "# human nation as prey, which would distort the game being measured."
		echo "# That bias is verified by playtesting, not by this harness."
		echo "#"
		echo "#"
		echo "# DAY COUNT MARGIN: under these pinned knobs the seed's game runs to day"
		echo "# 15411 before goal_destroy_nation_achieved() fires (one nation left"
		echo "# standing), measured with -headless-ai-days 100000. AIH_DAYS below sits"
		echo "# well under that. Re-measure that number before raising AIH_DAYS, and"
		echo "# before changing any knob above -- both move it."
		echo "#"
		echo "# Captured: $(date -u +%Y-%m-%dT%H:%M:%SZ) at commit $(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
		echo "AIH_DAYS=$DAYS"
		echo "AIH_REPEATS=$REPEATS"
		echo "AIH_METRIC_SERIES_SHA256=$METRIC_SHA"
		echo "AIH_FINAL_CRC=$(grep '^AIH_FINAL_CRC=' "$REF" | cut -d= -f2)"
		echo "AIH_NATIONS_ALIVE=$(grep '^AIH_NATIONS_ALIVE=' "$REF" | cut -d= -f2)"
		echo "AIH_WORLD_UNITS=$(grep '^AIH_WORLD_UNITS=' "$REF" | cut -d= -f2)"
		echo "AIH_WORLD_FIRMS=$(grep '^AIH_WORLD_FIRMS=' "$REF" | cut -d= -f2)"
		echo "AIH_WORLD_TOWNS=$(grep '^AIH_WORLD_TOWNS=' "$REF" | cut -d= -f2)"
		echo "# --- pinned scenario knobs, as recorded by the binary ---"
		cat "$WORK/knobs.txt"
		echo "# --- end-of-run summary ---"
		sed -n '/^AIH_SUMMARY_BEGIN=/,/^AIH_SUMMARY_END=/p' "$REF" | sed 's/^/# /'
		echo "# --- per-day CRC series (day crc) ---"
		cat "$WORK/crc_series.txt"
	} > "$BASELINE_FILE"
	echo "Baseline written to $BASELINE_FILE" >&2
	exit "$FAIL"
fi

if [ "$RUN_MAIN" = "1" ]; then
	if [ ! -f "$BASELINE_FILE" ]; then
		echo "ERROR: no baseline at $BASELINE_FILE. Run with --update-baseline first." >&2
		exit 1
	fi

	if [ "$DAYS_OVERRIDDEN" = "1" ]; then
		echo "NOTE: --days given; skipping baseline comparison (a series captured over a" >&2
		echo "      different day count is not comparable)." >&2
	else
		# knobs
		if diff -q <(grep -E '^AIH_' "$BASELINE_FILE" \
				| sed -n '/^AIH_VERSION=/,/^AIH_HEADER_END=/p') "$WORK/knobs.txt" >/dev/null; then
			echo "PASS: [ai] pinned scenario knobs match baseline" >&2
		else
			echo "FAIL: [ai] pinned scenario knobs drifted from the baseline:" >&2
			diff <(grep -E '^AIH_' "$BASELINE_FILE" \
				| sed -n '/^AIH_VERSION=/,/^AIH_HEADER_END=/p') "$WORK/knobs.txt" >&2 || true
			echo "      The baseline's numbers were measured under different conditions," >&2
			echo "      so comparing against them is meaningless until this is resolved." >&2
			FAIL=1
		fi

		# CRC series
		sed -n '/^AIH_D /p' "$BASELINE_FILE" > "$WORK/baseline_crc.txt"
		if diff -q "$WORK/baseline_crc.txt" "$WORK/crc_series.txt" >/dev/null; then
			echo "PASS: [ai] per-day CRC series matches baseline ($DAYS days)" >&2
		else
			echo "FAIL: [ai] per-day CRC series differs from baseline." >&2
			echo "      First divergence: $(first_crc_divergence "$REF" "$WORK/baseline_crc.txt")" >&2
			echo "      If a Phase 4 toggle is deliberately enabled, this is expected --" >&2
			echo "      the day above is where its effect first reaches simulation state." >&2
			echo "      If nothing was meant to change, this is the regression." >&2
			FAIL=1
		fi

		# metric series digest
		BASE_METRIC_SHA="$(grep '^AIH_METRIC_SERIES_SHA256=' "$BASELINE_FILE" | cut -d= -f2)"
		if [ "$BASE_METRIC_SHA" = "$METRIC_SHA" ]; then
			echo "PASS: [ai] per-day metric series digest matches baseline" >&2
		else
			echo "FAIL: [ai] per-day metric series differs from baseline" >&2
			echo "      baseline sha256=$BASE_METRIC_SHA" >&2
			echo "      this run     =$METRIC_SHA" >&2
			echo "      Re-run with --dump DIR and diff the AIH_N rows to see which nation" >&2
			echo "      and which column moved." >&2
			FAIL=1
		fi
	fi

	echo "-- [ai] diagnostics (not asserted): wall/busy/frames from the last run --" >&2
	grep -E '^AIH_(WALLTIME_MS|BUSY_MS|FRAME_ITERS|LOOP_ITERS|WORLD_)' "$REF" | sed 's/^/   /' >&2
fi

exit "$FAIL"
