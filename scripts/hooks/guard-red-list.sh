#!/usr/bin/env bash
# PreToolUse hook: blocks Edit/Write on 7KAA's red-listed files.
# See CLAUDE.md "Red list" section for why each entry is protected.
#
# Reads the PreToolUse JSON payload on stdin, matching Claude Code's hook
# contract: {"tool_name": "...", "tool_input": {"file_path": "...", ...}, ...}
#
# Exit 0  -> allow (no match, or matched but acknowledged via SEVENKAA_REDLIST_ACK)
# Exit 2  -> block; stderr message is surfaced to the agent/user
#
# Matching is done on a CANONICALISED repo-relative path: the incoming
# file_path is made absolute against the repo root, then '.', '..', repeated
# and trailing slashes are collapsed (and symlinks resolved, where realpath is
# available). Without this, './src/OMP_CRC.cpp', 'src/../src/OMP_CRC.cpp' and
# 'src//OMP_CRC.cpp' all name a protected file while comparing unequal to it.

set -euo pipefail

REPO_ROOT="${CLAUDE_PROJECT_DIR:-}"
if [ -z "$REPO_ROOT" ]; then
  REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi

PAYLOAD="$(cat)"
# Edit/Write carry file_path. The settings.json matcher is an unanchored
# regex ("Edit|Write"), so it also fires for NotebookEdit, which names its
# target notebook_path instead -- read both rather than fall through to allow.
FILE_PATH="$(printf '%s' "$PAYLOAD" \
  | jq -r '.tool_input.file_path // .tool_input.notebook_path // empty' 2>/dev/null || true)"

# Nothing to check (e.g. a tool call with no path) -> allow.
if [ -z "$FILE_PATH" ]; then
  exit 0
fi

# Lexical canonicalisation, used when realpath(1) is unavailable. Takes an
# absolute path, collapses '', '.' and '..' segments, and re-joins. Does not
# resolve symlinks -- realpath is preferred for exactly that reason.
normalize_path() {
  local path="$1"
  local seg
  local -a out=()
  local IFS='/'
  for seg in $path; do
    case "$seg" in
      '' | '.')
        ;;
      '..')
        if [ "${#out[@]}" -gt 0 ]; then
          unset 'out[${#out[@]}-1]'
        fi
        ;;
      *)
        out+=("$seg")
        ;;
    esac
  done
  if [ "${#out[@]}" -eq 0 ]; then
    printf '/'
  else
    printf '/%s' "${out[@]}"
  fi
}

# Make the incoming path absolute. A relative file_path is relative to the
# repo root, NOT to this hook process's working directory -- the hook is
# spawned by Claude Code and its cwd is not part of the contract.
case "$FILE_PATH" in
  /*) ABS_PATH="$FILE_PATH" ;;
  *)  ABS_PATH="$REPO_ROOT/$FILE_PATH" ;;
esac

if command -v realpath >/dev/null 2>&1; then
  CANON_PATH="$(realpath -m -- "$ABS_PATH")"
  CANON_ROOT="$(realpath -m -- "$REPO_ROOT")"
else
  CANON_PATH="$(normalize_path "$ABS_PATH")"
  CANON_ROOT="$(normalize_path "$REPO_ROOT")"
fi

case "$CANON_PATH" in
  "$CANON_ROOT"/*)
    REL_PATH="${CANON_PATH#"$CANON_ROOT"/}"
    ;;
  *)
    # Resolves outside the repo root entirely -> not something we protect.
    exit 0
    ;;
esac

# The red list. Keep in sync with CLAUDE.md's "Red list" section. Entries are
# repo-relative canonical spellings, matched as globs against REL_PATH.
RED_LIST=(
  "src/OGFILE2.cpp"
  "src/OGFILE3.cpp"
  "src/OMP_CRC.cpp"
  "configure.ac"
  "src/OSPATH.cpp"
  "src/ONATIONB.cpp"
  "src/OAI_*.cpp"
  "src/OTOWNAI.cpp"
  "src/OUNITAI.cpp"
  "src/OFIRMAI.cpp"
  # The regression harness and its baseline. A failing harness run means
  # something changed; editing the baseline to match an unreviewed result, or
  # loosening the script's checks, silences the safety net instead of fixing
  # what it caught. See CLAUDE.md.
  "scripts/phase0_harness.sh"
  "scripts/phase0_baseline.txt"
  # The Phase 4 AI-behaviour harness and its baseline, listed for the same
  # reason as the Phase 0 pair above. It is the only signal that catches an
  # AI change the multiplayer CRC cannot see -- that CRC hashes NationBase
  # only, never the AI state in Nation -- so re-recording its baseline to
  # match an unreviewed result is exactly the silent-failure mode the Phase 0
  # entries exist to prevent. See CLAUDE.md.
  "scripts/phase4_ai_harness.sh"
  "scripts/phase4_ai_baseline.txt"
  # The hook's own protection surface. A stuck agent that can't get past a
  # block will predictably try to edit or unregister the hook as its own
  # "fix" -- without this, the red list is only as strong as the agent's
  # patience. settings.local.json belongs here as much as settings.json: it
  # is untracked (.gitignore:112) and takes precedence, so an edit there is
  # both invisible to review and authoritative, and can grant blanket Bash
  # permission -- which this hook does not intercept at all. See CLAUDE.md.
  "scripts/hooks/guard-red-list.sh"
  ".claude/settings.json"
  ".claude/settings.local.json"
)

REASON=""
for pattern in "${RED_LIST[@]}"; do
  # shellcheck disable=SC2053  # intentional glob match, not literal string compare
  if [[ "$REL_PATH" == $pattern ]]; then
    REASON="$pattern"
    break
  fi
done

if [ -z "$REASON" ]; then
  exit 0
fi

# Escape hatch: SEVENKAA_REDLIST_ACK is a comma-separated list of relative
# paths/glob patterns explicitly acknowledged for this session. It is meant
# to be set from the LAUNCH environment (before Claude Code starts), never
# from inside the session -- since this hook process is spawned by Claude
# Code itself, not by the agent's own Bash tool calls, an `export` from a
# Bash tool call should not reach it. This has been verified empirically;
# see the verification note in CLAUDE.md / the Phase 4 command file.
#
# Entries are matched against the canonical REL_PATH, so they should be
# written repo-relative ("src/OMP_CRC.cpp"); a leading "./" and surrounding
# whitespace are tolerated.
if [ -n "${SEVENKAA_REDLIST_ACK:-}" ]; then
  IFS=',' read -ra ACK_PATTERNS <<< "$SEVENKAA_REDLIST_ACK"
  for ack in "${ACK_PATTERNS[@]}"; do
    ack="${ack#"${ack%%[![:space:]]*}"}"   # strip leading whitespace
    ack="${ack%"${ack##*[![:space:]]}"}"   # strip trailing whitespace
    ack="${ack#./}"
    [ -n "$ack" ] || continue
    # shellcheck disable=SC2053
    if [[ "$REL_PATH" == $ack ]]; then
      exit 0
    fi
  done
fi

cat >&2 <<EOF
BLOCKED by 7KAA red-list guard: $REL_PATH

This file is protected (matched red-list pattern: $REASON).
See CLAUDE.md's "Red list" section for why -- in short, this file is either
part of the multiplayer determinism/CRC contract, the save-game format, the
determinism-related build flags, AI decision logic that drives CRC-synced
unit orders, the regression harness that would catch a break in any of those,
or the guard mechanism itself.

Do not try to work around this by editing the hook or .claude/settings.json --
those are protected for exactly this reason, and neither is rewriting the path
a different way: matching is done on the canonicalised path. Get explicit human
sign-off first. If this session has been authorized (e.g. a Phase 4 AI-overhaul
session), the human should have set SEVENKAA_REDLIST_ACK in the launch
environment before this session started; it cannot be set from inside the
session.
EOF
exit 2
