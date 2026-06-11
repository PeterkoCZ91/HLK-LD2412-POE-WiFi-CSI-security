#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  tools/pull_ota_deploy.sh --host IP --firmware firmware.bin --expect-mac MAC [options] [--flash]
  tools/pull_ota_deploy.sh --host IP --firmware firmware.bin --expect-id MQTT_ID [options] [--flash]

Safe by default: without --flash this only verifies target identity and artifact hash.
At least one identity check is required: --expect-mac, --expect-id, or --expect-hostname.

Options:
  --host IP_OR_HOST          Target ESP HTTP host/IP.
  --firmware PATH           Firmware .bin to serve and Pull OTA.
  --user USER               HTTP auth user, default admin.
  --pass PASS               HTTP auth password, default admin.
  --expect-mac MAC          Expected chip/ETH MAC. Separators/case ignored.
  --expect-id MQTT_ID       Expected /api/health mqtt.id.
  --expect-hostname NAME    Expected /api/health hostname.
  --expect-current-fw VER   Optional expected current /api/version.
  --expect-new-fw VER       Optional expected version after reboot.
  --serve-ip IP             Local LAN IP device should fetch from. Auto-detected by route.
  --port PORT               Temporary HTTP server port, default 8001.
  --flash                   Actually perform Pull OTA.
  --cold-reboot             Before flashing, reboot the target and wait for it to
                            come back. Defragments the heap and clears AsyncTCP
                            state — recommended for units with long uptime.
                            Ignored in dry-run (no --flash).
  --help                    Show this help.

Example dry-run:
  tools/pull_ota_deploy.sh --host 192.168.1.50 \
    --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
    --expect-mac AA:BB:CC:DD:EE:FF --expect-id poe2412_node

Example flash (long-uptime unit):
  tools/pull_ota_deploy.sh --host 192.168.1.50 \
    --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
    --expect-mac AA:BB:CC:DD:EE:FF --expect-id poe2412_node --cold-reboot --flash
USAGE
}

HOST=""
FIRMWARE=""
USER="admin"
PASS="admin"
EXPECT_MAC=""
EXPECT_ID=""
EXPECT_HOSTNAME=""
EXPECT_CURRENT_FW=""
EXPECT_NEW_FW=""
SERVE_IP=""
PORT="8001"
DO_FLASH=0
COLD_REBOOT=0
SERVER_PID=""
LOG_FILE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --host) HOST="${2:-}"; shift 2 ;;
    --firmware) FIRMWARE="${2:-}"; shift 2 ;;
    --user) USER="${2:-}"; shift 2 ;;
    --pass) PASS="${2:-}"; shift 2 ;;
    --expect-mac) EXPECT_MAC="${2:-}"; shift 2 ;;
    --expect-id) EXPECT_ID="${2:-}"; shift 2 ;;
    --expect-hostname) EXPECT_HOSTNAME="${2:-}"; shift 2 ;;
    --expect-current-fw) EXPECT_CURRENT_FW="${2:-}"; shift 2 ;;
    --expect-new-fw) EXPECT_NEW_FW="${2:-}"; shift 2 ;;
    --serve-ip) SERVE_IP="${2:-}"; shift 2 ;;
    --port) PORT="${2:-}"; shift 2 ;;
    --flash) DO_FLASH=1; shift ;;
    --cold-reboot) COLD_REBOOT=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

cleanup() {
  if [ -n "${SERVER_PID}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

[ -n "$HOST" ] || fail "--host is required"
[ -n "$FIRMWARE" ] || fail "--firmware is required"
[ -f "$FIRMWARE" ] || fail "firmware not found: $FIRMWARE"
[ -n "$EXPECT_MAC$EXPECT_ID$EXPECT_HOSTNAME" ] || fail "at least one identity check is required"

FW_BASE=$(basename "$FIRMWARE")
FW_DIR=$(cd "$(dirname "$FIRMWARE")" && pwd)
case "$FW_BASE" in
  *[!A-Za-z0-9._-]*) fail "firmware filename must contain only A-Z a-z 0-9 dot underscore dash: $FW_BASE" ;;
esac

json_get() {
  python3 -c 'import json,sys; d=json.load(sys.stdin); p=sys.argv[1].split("."); v=d
for k in p:
    v = v.get(k, "") if isinstance(v, dict) else ""
print(v if v is not None else "")' "$1"
}

norm_mac() {
  printf '%s' "$1" | tr '[:lower:]' '[:upper:]' | tr -cd '0-9A-F'
}

curl_auth() {
  curl -sS --connect-timeout 5 --digest -u "${USER}:${PASS}" "$@"
}

CURRENT_FW=$(curl -sS --connect-timeout 5 "http://${HOST}/api/version" || true)
[ -n "$CURRENT_FW" ] || fail "target does not answer /api/version"

HEALTH_JSON=$(curl_auth "http://${HOST}/api/health")
ACTUAL_MAC=$(printf '%s' "$HEALTH_JSON" | json_get ethernet.mac)
if [ -z "$ACTUAL_MAC" ]; then
  ACTUAL_MAC=$(printf '%s' "$HEALTH_JSON" | json_get chip.mac)
fi
ACTUAL_ID=$(printf '%s' "$HEALTH_JSON" | json_get mqtt.id)
ACTUAL_HOSTNAME=$(printf '%s' "$HEALTH_JSON" | json_get hostname)
ACTUAL_ETH_IP=$(printf '%s' "$HEALTH_JSON" | json_get ethernet.ip)
ACTUAL_HEALTH_FW=$(printf '%s' "$HEALTH_JSON" | json_get fw_version)

if [ -n "$EXPECT_CURRENT_FW" ] && [ "$CURRENT_FW" != "$EXPECT_CURRENT_FW" ]; then
  fail "current firmware mismatch: expected $EXPECT_CURRENT_FW, got $CURRENT_FW"
fi
if [ -n "$EXPECT_MAC" ] && [ "$(norm_mac "$EXPECT_MAC")" != "$(norm_mac "$ACTUAL_MAC")" ]; then
  fail "MAC mismatch: expected $EXPECT_MAC, got $ACTUAL_MAC"
fi
if [ -n "$EXPECT_ID" ] && [ "$EXPECT_ID" != "$ACTUAL_ID" ]; then
  fail "MQTT id mismatch: expected $EXPECT_ID, got $ACTUAL_ID"
fi
if [ -n "$EXPECT_HOSTNAME" ] && [ "$EXPECT_HOSTNAME" != "$ACTUAL_HOSTNAME" ]; then
  fail "hostname mismatch: expected $EXPECT_HOSTNAME, got $ACTUAL_HOSTNAME"
fi

FW_MD5=$(md5sum "$FIRMWARE" | awk '{print $1}')
FW_SIZE=$(wc -c < "$FIRMWARE" | tr -d ' ')

echo "Target verified"
echo "  host:       $HOST"
echo "  eth_ip:     $ACTUAL_ETH_IP"
echo "  fw:         $CURRENT_FW (health: $ACTUAL_HEALTH_FW)"
echo "  mac:        $ACTUAL_MAC"
echo "  mqtt.id:    $ACTUAL_ID"
echo "  hostname:   $ACTUAL_HOSTNAME"
echo "Artifact"
echo "  file:       $FIRMWARE"
echo "  size:       $FW_SIZE"
echo "  md5:        $FW_MD5"

if [ "$DO_FLASH" -ne 1 ]; then
  if [ "$COLD_REBOOT" -eq 1 ]; then
    echo "Note: --cold-reboot is ignored in dry-run; add --flash to actually reboot and flash."
  fi
  echo "Dry-run complete. Re-run with --flash to perform Pull OTA."
  exit 0
fi

# Cold reboot before flashing. Long uptime fragments the heap and loads
# AsyncTCP, which is the usual reason a fresh-flash-OK OTA fails on an
# in-service unit. Identity was already verified above, so we are rebooting
# the intended device.
if [ "$COLD_REBOOT" -eq 1 ]; then
  echo "Cold reboot requested — rebooting target to defragment heap and clear AsyncTCP state"
  curl_auth -X POST "http://${HOST}/api/restart" >/dev/null 2>&1 || true
  echo "  waiting for device to come back..."
  sleep 5
  BACK=0
  for i in $(seq 1 45); do
    RBV=$(curl -sS --connect-timeout 3 "http://${HOST}/api/version" 2>/dev/null || true)
    if [ -n "$RBV" ]; then
      if [ -n "$EXPECT_CURRENT_FW" ] && [ "$RBV" != "$EXPECT_CURRENT_FW" ]; then
        fail "after reboot version is '$RBV', expected '$EXPECT_CURRENT_FW' — aborting"
      fi
      BACK=1
      echo "  device back online: $RBV"
      break
    fi
    sleep 2
  done
  [ "$BACK" -eq 1 ] || fail "device did not come back within ~95s after cold reboot"
fi

if [ -z "$SERVE_IP" ]; then
  SERVE_IP=$(ip route get "$HOST" 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)
fi
[ -n "$SERVE_IP" ] || fail "could not auto-detect --serve-ip"

LOG_FILE="/tmp/poe_pull_ota_${PORT}.log"
python3 -u -m http.server "$PORT" --bind 0.0.0.0 --directory "$FW_DIR" >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1
kill -0 "$SERVER_PID" 2>/dev/null || fail "temporary HTTP server failed; see $LOG_FILE"

FW_URL="http://${SERVE_IP}:${PORT}/${FW_BASE}"
HEADERS=$(curl -I -sS --connect-timeout 5 "$FW_URL")
printf '%s\n' "$HEADERS" | grep -q '200 OK' || fail "firmware URL did not return 200: $FW_URL"
printf '%s\n' "$HEADERS" | grep -q "Content-Length: ${FW_SIZE}" || fail "firmware URL size mismatch at $FW_URL"

echo "Serving artifact"
echo "  url:        $FW_URL"
echo "  log:        $LOG_FILE"

BODY=$(FW_URL="$FW_URL" FW_MD5="$FW_MD5" python3 -c 'import json,os; print(json.dumps({"url": os.environ["FW_URL"], "md5": os.environ["FW_MD5"]}))')
echo "Starting Pull OTA..."
RESP=$(curl_auth -H 'Content-Type: application/json' -d "$BODY" "http://${HOST}/api/update/pull")
echo "  response: $RESP"

for i in $(seq 1 120); do
  sleep 2
  STATUS=$(curl_auth "http://${HOST}/api/update/pull/status" || true)
  PHASE=$(printf '%s' "$STATUS" | python3 -c 'import json,sys
try:
    print(json.load(sys.stdin).get("phase", "unknown"))
except Exception:
    print("unreachable")')
  MSG=$(printf '%s' "$STATUS" | python3 -c 'import json,sys
try:
    print(json.load(sys.stdin).get("message", ""))
except Exception:
    print("")')
  echo "  phase: $PHASE ${MSG}"
  case "$PHASE" in
    success|success_rebooting) break ;;
    failed|error) fail "Pull OTA failed: $STATUS" ;;
  esac
done

if [ "$PHASE" != "success" ] && [ "$PHASE" != "success_rebooting" ]; then
  fail "Pull OTA did not finish before timeout"
fi

echo "Waiting for device version..."
for i in $(seq 1 60); do
  sleep 2
  NEW_FW=$(curl -sS --connect-timeout 3 "http://${HOST}/api/version" 2>/dev/null || true)
  if [ -n "$NEW_FW" ]; then
    echo "  version: $NEW_FW"
    if [ -n "$EXPECT_NEW_FW" ] && [ "$NEW_FW" != "$EXPECT_NEW_FW" ]; then
      fail "new firmware mismatch: expected $EXPECT_NEW_FW, got $NEW_FW"
    fi
    echo "Pull OTA complete"
    exit 0
  fi
done

fail "device did not come back on /api/version"
