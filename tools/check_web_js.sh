#!/usr/bin/env bash
# C-3 / T7: syntax-check the dashboard JavaScript sources.
#
# Since T7 the dashboard lives split under web/src/{i18n.js,app.js} (built
# into include/web_interface.h by tools/build_web.py) instead of inline in a
# C++ raw string literal — the compiler never parsed it either way, so an
# unbalanced brace or stray `)` still builds green and only fails in the
# browser. This checks the sources directly with `node --check`
# (parse-only — DOM globals and cross-file references are not resolved, so
# no false positives from `document`, `fetch`, template literals, etc.).
#
# Usage: tools/check_web_js.sh [web/src dir]
set -euo pipefail

SRC_DIR="${1:-web/src}"

if ! command -v node >/dev/null 2>&1; then
  echo "check_web_js: node not found on PATH" >&2
  exit 2
fi

for f in "$SRC_DIR/i18n.js" "$SRC_DIR/app.js"; do
  if [ ! -f "$f" ]; then
    echo "check_web_js: source not found: $f" >&2
    exit 2
  fi
  LINES=$(wc -l < "$f" | tr -d ' ')
  echo "check_web_js: $f — $LINES lines"
  node --check "$f"
done

# In the browser i18n.js and app.js are two <script> tags on the same page,
# sharing one global lexical scope — a top-level `const`/`let`/`function`
# redeclared in app.js that already exists in i18n.js (or vice versa) is a
# real runtime error there too, but `node --check` on each file alone can't
# see it (each is its own module scope). Concatenate and check once more.
TMP="$(mktemp --suffix=.js)"
trap 'rm -f "$TMP"' EXIT
cat "$SRC_DIR/i18n.js" "$SRC_DIR/app.js" > "$TMP"
node --check "$TMP"
echo "check_web_js: JS syntax OK (individually and concatenated)"
