#!/usr/bin/env bash
# PreToolUse hook: blocks Edit/Write on 7KAA's red-listed files.
# See CLAUDE.md "Red list" section for why each entry is protected.
#
# Reads the PreToolUse JSON payload on stdin, matching Claude Code's hook
# contract: {"tool_name": "...", "tool_input": {"file_path": "...", ...}, ...}
#
# Exit 0  -> allow (no match, or matched but acknowledged via SEVENKAA_REDLIST_ACK)
# Exit 2  -> block; stderr message is surfaced to the agent/user

set -euo pipefail

REPO_ROOT="${CLAUDE_PROJECT_DIR:-}"
if [ -z "$REPO_ROOT" ]; then
  REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi

PAYLOAD="$(cat)"
FILE_PATH="$(printf '%s' "$PAYLOAD" | jq -r '.tool_input.file_path // empty' 2>/dev/null || true)"

# Nothing to check (e.g. a tool call with no file_path) -> allow.
if [ -z "$FILE_PATH" ]; then
  exit 0
fi

# Normalize to a path relative to the repo root so patterns below are stable
# regardless of whether the tool passed an absolute or relative path.
case "$FILE_PATH" in
  "$REPO_ROOT"/*)
    REL_PATH="${FILE_PATH#"$REPO_ROOT"/}"
    ;;
  /*)
    # Absolute path outside the repo root entirely -> not something we protect.
    exit 0
    ;;
  *)
    REL_PATH="$FILE_PATH"
    ;;
esac

# The red list. Keep in sync with CLAUDE.md's "Red list" section.
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
  # The hook's own protection surface. A stuck agent that can't get past a
  # block will predictably try to edit or unregister the hook as its own
  # "fix" -- without this, the red list is only as strong as the agent's
  # patience. See CLAUDE.md.
  "scripts/hooks/guard-red-list.sh"
  ".claude/settings.json"
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
if [ -n "${SEVENKAA_REDLIST_ACK:-}" ]; then
  IFS=',' read -ra ACK_PATTERNS <<< "$SEVENKAA_REDLIST_ACK"
  for ack in "${ACK_PATTERNS[@]}"; do
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
unit orders, or the guard mechanism itself.

Do not try to work around this by editing the hook or .claude/settings.json --
those are protected for exactly this reason. Get explicit human sign-off
first. If this session has been authorized (e.g. a Phase 4 AI-overhaul
session), the human should have set SEVENKAA_REDLIST_ACK in the launch
environment before this session started; it cannot be set from inside the
session.
EOF
exit 2
