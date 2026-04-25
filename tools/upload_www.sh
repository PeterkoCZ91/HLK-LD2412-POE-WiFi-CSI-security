#!/bin/bash
# Upload web interface to ESP32 LittleFS via HTTP
# Usage: ./tools/upload_www.sh [host] [user] [pass]
#
# Gzips include/web_interface.h HTML and uploads to /index.html.gz
# After upload, device serves the LittleFS version (smaller, faster)
# Revert to PROGMEM: curl -X DELETE http://host/api/www -u user:pass

HOST="${1:-192.168.X.X}"
USER="${2:-admin}"
PASS="${3:-admin}"

SRC="include/web_interface.h"
TMP="/tmp/index_poe.html"
GZ="/tmp/index_poe.html.gz"

if [ ! -f "$SRC" ]; then
    echo "ERROR: $SRC not found — run from project root"
    exit 1
fi

echo "==> Extracting HTML from $SRC..."
# Strip C header wrapper: remove first line (const char...) and last 2 lines ()rawliteral"; #endif)
tail -n +5 "$SRC" | head -n -3 > "$TMP"
HTML_SIZE=$(wc -c < "$TMP")
echo "    HTML size: ${HTML_SIZE} bytes"

echo "==> Gzipping..."
gzip -9 -f -k "$TMP" 2>/dev/null || gzip -9 -f "$TMP"
mv "${TMP}.gz" "$GZ" 2>/dev/null || true
GZ_SIZE=$(wc -c < "$GZ")
echo "    GZ size:   ${GZ_SIZE} bytes ($(( GZ_SIZE * 100 / HTML_SIZE ))% of original)"

echo "==> Uploading to http://${HOST}/api/www/upload ..."
HTTP_CODE=$(curl -s -o /tmp/upload_response.txt -w "%{http_code}" \
    -u "${USER}:${PASS}" \
    -X POST \
    -F "file=@${GZ};filename=index.html.gz" \
    "http://${HOST}/api/www/upload")

RESPONSE=$(cat /tmp/upload_response.txt)
echo "    HTTP ${HTTP_CODE}: ${RESPONSE}"

if [ "$HTTP_CODE" = "200" ]; then
    echo ""
    echo "==> Verifying..."
    curl -s -u "${USER}:${PASS}" "http://${HOST}/api/www/info" | python3 -m json.tool 2>/dev/null || \
    curl -s -u "${USER}:${PASS}" "http://${HOST}/api/www/info"
    echo ""
    echo "✓ Done — GUI now served from LittleFS"
    echo "  Revert: curl -X DELETE http://${HOST}/api/www -u ${USER}:${PASS}"
else
    echo "✗ Upload failed"
    exit 1
fi

rm -f "$TMP" "$GZ" /tmp/upload_response.txt
