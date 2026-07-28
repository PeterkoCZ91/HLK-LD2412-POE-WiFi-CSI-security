#!/usr/bin/env bash
# C-3: syntax-check the JavaScript embedded in the web GUI header.
#
# The dashboard JS lives inside a C++ raw string literal
# (`const char index_html[] PROGMEM = R"rawliteral(...)"`), so the C++
# compiler never parses it: an unbalanced brace, a stray `)` or a broken
# `for(` builds green and only fails in the browser at runtime. This script
# extracts every <script> block and runs it through `node --check`
# (parse-only — DOM globals and cross-block references are not resolved, so
# no false positives from `document`, `fetch`, template literals, etc.).
#
# Usage: tools/check_web_js.sh [path/to/web_interface.h]
set -euo pipefail

HDR="${1:-include/web_interface.h}"

if ! command -v node >/dev/null 2>&1; then
  echo "check_web_js: node not found on PATH" >&2
  exit 2
fi
if [ ! -f "$HDR" ]; then
  echo "check_web_js: header not found: $HDR" >&2
  exit 2
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
OUT="$TMPDIR/web.js"

# Concatenate the body of every <script>...</script> block. In the browser
# these blocks share one global lexical scope, so concatenating is faithful:
# a top-level redeclaration across blocks is a real error there too. `<script>`
# / `</script>` never appear inside a JS string literal (that would close the
# tag in the browser as well), so the plain toggle is safe.
awk '
  /<script>/   { injs = 1; next }
  /<\/script>/ { injs = 0; next }
  injs         { print }
' "$HDR" > "$OUT"

BLOCKS=$(grep -c '<script>' "$HDR" || true)
LINES=$(wc -l < "$OUT" | tr -d ' ')

if [ "${LINES:-0}" -eq 0 ]; then
  echo "check_web_js: no <script> content extracted from $HDR" >&2
  exit 1
fi

echo "check_web_js: $HDR — $BLOCKS <script> block(s), $LINES lines of JS"
node --check "$OUT"
echo "check_web_js: JS syntax OK"
