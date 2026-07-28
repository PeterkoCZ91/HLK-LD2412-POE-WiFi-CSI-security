#!/usr/bin/env bash
# C-3: fail the build when firmware flash or static RAM usage crosses a budget.
#
# PlatformIO already hard-fails the link when the image overflows its app
# partition or when IRAM overflows its fixed region — those are covered by the
# linker. This adds a *soft* budget that trips before the hard limit, so a
# sudden regression (an accidental large table, a runaway template
# instantiation) or steady creep toward the OTA partition ceiling fails CI
# early instead of only when the next OTA image no longer fits.
#
# Budget is on the reported percentage, which normalizes across the 8 MB and
# 16 MB partition layouts (each env's % is relative to *its own* app
# partition). Override via env vars: FLASH_MAX_PCT, RAM_MAX_PCT.
#
# Usage: tools/check_size_budget.sh <pio-env> [build-log-file]
#   With no log file it runs `pio run -e <env>` itself and parses its output.
#   The "Advanced Memory Usage" RAM/Flash percentage lines are only emitted by
#   a plain `pio run` (the link/size-check step) — NOT by `--target size`,
#   which prints raw text/data/bss section sizes with no partition context.
set -euo pipefail

ENV="${1:?usage: check_size_budget.sh <pio-env> [build-log-file]}"
LOG="${2:-}"
FLASH_MAX_PCT="${FLASH_MAX_PCT:-90}"
RAM_MAX_PCT="${RAM_MAX_PCT:-85}"

if [ -n "$LOG" ] && [ -f "$LOG" ]; then
  SIZE_OUT="$(cat "$LOG")"
else
  SIZE_OUT="$(pio run -e "$ENV" 2>&1)"
fi

# PlatformIO prints, e.g.:
#   RAM:   [===       ]  26.3% (used 86172 bytes from 327680 bytes)
#   Flash: [====      ]  39.0% (used 1636045 bytes from 4194304 bytes)
RAM_PCT="$(printf '%s\n' "$SIZE_OUT"   | grep -E '^RAM:'   | grep -oE '[0-9]+(\.[0-9]+)?%' | head -1 | tr -d '%')"
FLASH_PCT="$(printf '%s\n' "$SIZE_OUT" | grep -E '^Flash:' | grep -oE '[0-9]+(\.[0-9]+)?%' | head -1 | tr -d '%')"

if [ -z "$RAM_PCT" ] || [ -z "$FLASH_PCT" ]; then
  echo "check_size_budget: could not parse RAM/Flash usage for env $ENV" >&2
  printf '%s\n' "$SIZE_OUT" | grep -E '^(RAM|Flash):' >&2 || true
  exit 2
fi

echo "check_size_budget: $ENV — RAM ${RAM_PCT}% (budget ${RAM_MAX_PCT}%), Flash ${FLASH_PCT}% (budget ${FLASH_MAX_PCT}%)"

fail=0
if awk -v v="$RAM_PCT" -v m="$RAM_MAX_PCT" 'BEGIN{exit !(v+0 > m+0)}'; then
  echo "check_size_budget: FAIL — RAM ${RAM_PCT}% exceeds budget ${RAM_MAX_PCT}%" >&2
  fail=1
fi
if awk -v v="$FLASH_PCT" -v m="$FLASH_MAX_PCT" 'BEGIN{exit !(v+0 > m+0)}'; then
  echo "check_size_budget: FAIL — Flash ${FLASH_PCT}% exceeds budget ${FLASH_MAX_PCT}%" >&2
  fail=1
fi

[ "$fail" -eq 0 ] && echo "check_size_budget: within budget"
exit "$fail"
