#!/usr/bin/env bash
# Verifies every `<file> :: <Symbol>` citation in docs/remaster/FINDINGS.md
# and CLAUDE.md (or other files passed as args) still resolves against
# current source.
#
# This checks navigational citations only -- spans written in the single
# backtick `<file> :: <Symbol>` form. It does NOT check every file or symbol
# mentioned in these docs: bare file paths with no `::`, bare symbols with no
# file, directory/glob references, and prose mentions are silently skipped.
# A clean (exit 0) run means every citation IN THAT FORMAT resolved -- it
# does not mean every reference in the docs was checked.
#
# Two distinct, separately-reported failure modes:
#   1. FILE MISSING  -- the cited file no longer exists at that path (likely
#      a rename). Reported first per file, since it invalidates every
#      citation into that file at once.
#   2. SYMBOL MISSING -- the file exists but the cited symbol wasn't found
#      in it. Reported per citation.
#
# On any failure: this script reports it. It does NOT edit the target docs to
# make itself pass -- if a citation is wrong, the fix is either correcting
# the claim in the doc to match reality, or discovering the source
# genuinely changed and updating the citation deliberately. Never silently
# reword a claim just to satisfy this check.
#
# Exit 0 if every citation resolves, non-zero otherwise.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "$#" -gt 0 ]; then
  TARGETS=("$@")
else
  TARGETS=("$REPO_ROOT/docs/remaster/FINDINGS.md" "$REPO_ROOT/CLAUDE.md")
fi

TOTAL_CITATIONS=0
TOTAL_CHECKED=0
TOTAL_FILE_FAILURES=0
TOTAL_SYMBOL_FAILURES=0
TARGETS_MISSING=0

for TARGET in "${TARGETS[@]}"; do
  echo "check-citations.sh: checking $TARGET" >&2

  if [ ! -f "$TARGET" ]; then
    echo "check-citations.sh: target file not found: $TARGET" >&2
    TARGETS_MISSING=$((TARGETS_MISSING + 1))
    continue
  fi

  # Extract citations of the form `<file>.cpp :: Symbol` or `<file>.h :: Symbol`
  # from inside backticks. This intentionally only matches *.cpp/*.h paths so
  # it doesn't pick up unrelated inline-code spans that happen to contain "::".
  mapfile -t CITATIONS < <(grep -oE '`[A-Za-z0-9_./ -]+\.(cpp|h) :: [^`]+`' "$TARGET" | sed -E 's/^`//; s/`$//' | sort -u)

  if [ "${#CITATIONS[@]}" -eq 0 ]; then
    echo "check-citations.sh: no citations found in $TARGET -- nothing to check (this is likely wrong; verify the extraction regex still matches the doc's citation format)" >&2
    TARGETS_MISSING=$((TARGETS_MISSING + 1))
    continue
  fi

  TOTAL_CITATIONS=$((TOTAL_CITATIONS + ${#CITATIONS[@]}))
  declare -A FILE_EXISTS_CACHE

  for citation in "${CITATIONS[@]}"; do
    file_part="$(printf '%s' "$citation" | sed -E 's/ :: .*$//')"
    symbol_part="$(printf '%s' "$citation" | sed -E 's/^[^:]+:: //')"
    file_path="$REPO_ROOT/$file_part"

    if [ -z "${FILE_EXISTS_CACHE[$file_part]+x}" ]; then
      if [ -f "$file_path" ]; then
        FILE_EXISTS_CACHE[$file_part]="yes"
      else
        FILE_EXISTS_CACHE[$file_part]="no"
      fi
    fi

    if [ "${FILE_EXISTS_CACHE[$file_part]}" = "no" ]; then
      echo "FILE MISSING: '$file_part' (cited by: \`$citation\`, in $TARGET) -- likely renamed or moved. Every citation into this file needs review." >&2
      TOTAL_FILE_FAILURES=$((TOTAL_FILE_FAILURES + 1))
      continue
    fi

    TOTAL_CHECKED=$((TOTAL_CHECKED + 1))

    # Reduce the symbol to a grep-able base name:
    #  - strip a trailing parameter list, e.g. "scroll(int x, int y)" -> "scroll"
    #  - take the last "::"-scoped component, e.g. "Matrix::scroll" -> "scroll"
    # This deliberately does not verify exact signatures/overloads -- it
    # verifies the symbol name still exists somewhere in the cited file, which
    # is the durable claim the doc is making per its own citation policy.
    base_symbol="$(printf '%s' "$symbol_part" | sed -E 's/\(.*$//; s/^.*:://' | sed -E 's/[[:space:]]+$//')"

    if [ -z "$base_symbol" ]; then
      echo "SYMBOL MISSING: could not extract a checkable symbol from '$symbol_part' (cited by: \`$citation\`, in $TARGET)" >&2
      TOTAL_SYMBOL_FAILURES=$((TOTAL_SYMBOL_FAILURES + 1))
      continue
    fi

    if ! grep -qw -- "$base_symbol" "$file_path"; then
      echo "SYMBOL MISSING: '$base_symbol' not found in '$file_part' (cited by: \`$citation\`, in $TARGET)" >&2
      TOTAL_SYMBOL_FAILURES=$((TOTAL_SYMBOL_FAILURES + 1))
    fi
  done

  unset FILE_EXISTS_CACHE
done

TOTAL_FAILURES=$((TOTAL_FILE_FAILURES + TOTAL_SYMBOL_FAILURES + TARGETS_MISSING))
echo "" >&2
echo "check-citations.sh: ${#TARGETS[@]} target(s), $TOTAL_CITATIONS citations found, $TOTAL_CHECKED symbol-checked, $TOTAL_FILE_FAILURES file-missing failures, $TOTAL_SYMBOL_FAILURES symbol-missing failures, $TARGETS_MISSING target(s) missing/empty." >&2

if [ "$TOTAL_FAILURES" -gt 0 ]; then
  exit 1
fi
exit 0
