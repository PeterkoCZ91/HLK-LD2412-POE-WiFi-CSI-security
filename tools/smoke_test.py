#!/usr/bin/env python3
"""Smoke test for poe-2412-wifi firmware against a live device.

Usage:
    python3 tools/smoke_test.py --ip 192.168.1.50
    python3 tools/smoke_test.py --ip 192.168.1.50 --skip-alarm
"""
import argparse
import base64
import json
import sys
import time
import urllib.error
import urllib.request


# ── Result tracking ──────────────────────────────────────────────────────────

RESULTS = []   # list of (check_name, status, message)


def record(check, status, msg):
    """Record and immediately print one check result."""
    RESULTS.append((check, status, msg))
    print(f"  [{status:4s}] {check}: {msg}")


# ── HTTP helpers ─────────────────────────────────────────────────────────────

def _make_auth(user, password):
    token = base64.b64encode(f"{user}:{password}".encode()).decode()
    return f"Basic {token}"


def http_get(ip, path, auth=None, timeout=8):
    """GET request; returns (http_status, bytes_body). Raises on error."""
    req = urllib.request.Request(f"http://{ip}{path}")
    if auth:
        req.add_header("Authorization", auth)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def http_post(ip, path, auth=None, timeout=8):
    """POST request (no body); returns (http_status, bytes_body). Raises on error."""
    req = urllib.request.Request(f"http://{ip}{path}", method="POST")
    if auth:
        req.add_header("Authorization", auth)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


# ── Checks ───────────────────────────────────────────────────────────────────

def check_version(ip, auth, timeout):
    """GET /api/version (no auth) → non-empty string starting with 'v'."""
    try:
        _, body = http_get(ip, "/api/version", auth=None, timeout=timeout)
        text = body.decode().strip()
        if text.startswith("v") and len(text) > 1:
            record("version", "PASS", text)
        else:
            record("version", "FAIL", f"unexpected response: {text!r}")
    except Exception as exc:
        record("version", "FAIL", str(exc))


def check_healthz(ip, auth, timeout):
    """GET /healthz → JSON with 'fw' and 'uptime_s' fields."""
    try:
        _, body = http_get(ip, "/healthz", auth=auth, timeout=timeout)
        data = json.loads(body)
        if "fw" in data and "uptime_s" in data:
            record("healthz", "PASS", f"fw={data['fw']}  uptime={data['uptime_s']}s")
        else:
            record("healthz", "FAIL", f"missing required keys; got {sorted(data.keys())}")
    except Exception as exc:
        record("healthz", "FAIL", str(exc))


def check_health_core(ip, auth, timeout):
    """GET /api/health → sub-checks for heap, temp, OTA state.
    Returns the parsed JSON dict so downstream checks can reuse it."""
    try:
        _, body = http_get(ip, "/api/health", auth=auth, timeout=timeout)
        data = json.loads(body)
    except Exception as exc:
        record("health", "FAIL", str(exc))
        return None

    heap = data.get("free_heap", 0)
    if heap > 40000:
        record("health/free_heap", "PASS", f"{heap} bytes")
    elif heap >= 20000:
        record("health/free_heap", "WARN", f"{heap} bytes (low)")
    else:
        record("health/free_heap", "FAIL", f"{heap} bytes (critically low)")

    temp = data.get("chip_temp", 0.0)
    if temp < 75:
        record("health/chip_temp", "PASS", f"{temp:.1f} °C")
    elif temp <= 85:
        record("health/chip_temp", "WARN", f"{temp:.1f} °C (hot)")
    else:
        record("health/chip_temp", "FAIL", f"{temp:.1f} °C (overheating)")

    ota = data.get("ota_state", "unknown")
    if ota == "valid":
        record("health/ota_state", "PASS", ota)
    else:
        record("health/ota_state", "WARN", f"ota_state={ota!r}")

    return data


def check_mqtt(ip, auth, timeout, health_data):
    """From /api/health JSON: check MQTT connectivity and publish health."""
    if health_data is None:
        record("mqtt", "SKIP", "no health data available")
        return

    mqtt = health_data.get("mqtt", {})
    if not mqtt.get("enabled", False):
        record("mqtt", "SKIP", "mqtt.enabled=false — not configured")
        return

    if mqtt.get("connected", False):
        record("mqtt/connected", "PASS", f"connected to {mqtt.get('server')}:{mqtt.get('port')}")
    else:
        record("mqtt/connected", "FAIL", "mqtt.connected=false")

    streak = mqtt.get("publish_fail_streak", 0)
    if streak == 0:
        record("mqtt/publish_fail_streak", "PASS", "0")
    else:
        record("mqtt/publish_fail_streak", "WARN", f"streak={streak}")


def check_radar_csi(ip, auth, timeout, health_data):
    """From /api/health JSON: radar and CSI checks (radar adaptive, CSI always)."""
    if health_data is None:
        record("radar", "SKIP", "no health data available")
        record("csi", "SKIP", "no health data available")
        return

    if health_data.get("radar_monitoring_disabled", False):
        record("radar", "SKIP", "CSI-only unit (radar_monitoring_disabled=true)")
    else:
        uart = health_data.get("uart_state", "")
        rate = health_data.get("frame_rate", 0)
        if uart == "RUNNING" and rate > 0:
            record("radar", "PASS", f"uart_state={uart}  frame_rate={rate}")
        else:
            record("radar", "FAIL", f"uart_state={uart!r}  frame_rate={rate}")

    if health_data.get("csi_data_ok", False):
        record("csi", "PASS", "csi_data_ok=true")
    else:
        record("csi", "WARN", "csi_data_ok=false (weak signal — not a FW defect)")


def check_gui(ip, auth, timeout):
    """GET / → HTTP 200, HTML > 50 KB, >100 data-i18n occurrences."""
    try:
        _, body = http_get(ip, "/", auth=auth, timeout=timeout)
        html = body.decode(errors="replace")
        size = len(body)
        i18n_count = html.count("data-i18n")

        if size <= 50000:
            record("gui", "FAIL", f"HTML too small: {size} bytes (need >50 KB)")
        elif i18n_count <= 100:
            record("gui", "FAIL", f"data-i18n count too low: {i18n_count} (need >100)")
        else:
            record("gui", "PASS", f"size={size} bytes  data-i18n={i18n_count}")
    except Exception as exc:
        record("gui", "FAIL", str(exc))


def check_alarm(ip, auth, timeout):
    """Alarm FSM cycle: get state → arm (delayed) → verify arming → disarm → verify disarmed.
    Skips if system is not in disarmed state. Always ends with a disarm attempt (try/finally)."""

    def get_alarm_state():
        _, body = http_get(ip, "/api/alarm/status", auth=auth, timeout=timeout)
        return json.loads(body)

    # Initial state check — abort if system is not idle
    try:
        s = get_alarm_state()
    except Exception as exc:
        record("alarm", "FAIL", f"cannot read alarm status: {exc}")
        return

    if s["state"] != "disarmed":
        record("alarm", "SKIP",
               f"system is {s['state']!r} — skipping cycle to avoid disruption")
        return

    # Arm → check arming → disarm (always via finally)
    try:
        http_post(ip, "/api/alarm/arm", auth=auth, timeout=timeout)
        time.sleep(0.5)
        s = get_alarm_state()
        if s["state"] == "arming":
            record("alarm/arm", "PASS", "state=arming")
        else:
            record("alarm/arm", "FAIL", f"expected arming, got {s['state']!r}")
    except Exception as exc:
        record("alarm/arm", "FAIL", str(exc))
    finally:
        try:
            http_post(ip, "/api/alarm/disarm", auth=auth, timeout=timeout)
            time.sleep(0.5)
            s = get_alarm_state()
            if s["state"] == "disarmed":
                record("alarm/disarm", "PASS", "state=disarmed")
            else:
                record("alarm/disarm", "FAIL", f"expected disarmed, got {s['state']!r}")
        except Exception as exc:
            record("alarm/disarm", "FAIL", f"disarm failed: {exc}")


def check_site_learning(ip, auth, timeout):
    """Site-learning API contract (README-compatible action= aliases + unknown-action guard):
    - POST ?action=status (unknown action) → HTTP 400, learning must NOT start
    - POST ?action=start&duration_s=3600  → HTTP 200, learning_active=true
    - POST ?action=stop                    → HTTP 200, learning_active=false
    Skips if CSI is inactive or learning is already running. Always ends with ?stop=1."""

    def get_csi():
        _, body = http_get(ip, "/api/csi", auth=auth, timeout=timeout)
        return json.loads(body)

    def post_sl(query):
        """POST /api/csi/site_learning?<query> → (status, body); HTTPError → its code."""
        try:
            return http_post(ip, f"/api/csi/site_learning?{query}", auth=auth, timeout=timeout)
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()

    # Precondition — abort if CSI is down or learning already in progress
    try:
        csi = get_csi()
    except Exception as exc:
        record("site_learning", "SKIP", f"cannot read /api/csi: {exc}")
        return

    if not csi.get("active", False):
        record("site_learning", "SKIP", "CSI inactive")
        return
    if csi.get("learning_active", False):
        record("site_learning", "SKIP", "learning already running — not interfering")
        return

    try:
        # Unknown action value must be rejected, never fall through to a silent start
        status, _ = post_sl("action=status")
        time.sleep(0.5)
        active = get_csi().get("learning_active", False)
        if status == 400 and not active:
            record("site_learning/unknown_action", "PASS", "HTTP 400, learning not started")
        else:
            record("site_learning/unknown_action", "FAIL",
                   f"HTTP {status}, learning_active={active} (expected 400 + false)")

        # README-documented alias: action=start
        status, _ = post_sl("action=start&duration_s=3600")
        time.sleep(0.5)
        active = get_csi().get("learning_active", False)
        if status == 200 and active:
            record("site_learning/action_start", "PASS", "HTTP 200, learning_active=true")
        else:
            record("site_learning/action_start", "FAIL",
                   f"HTTP {status}, learning_active={active} (expected 200 + true)")

        # README-documented alias: action=stop
        status, _ = post_sl("action=stop")
        time.sleep(0.5)
        active = get_csi().get("learning_active", False)
        if status == 200 and not active:
            record("site_learning/action_stop", "PASS", "HTTP 200, learning_active=false")
        else:
            record("site_learning/action_stop", "FAIL",
                   f"HTTP {status}, learning_active={active} (expected 200 + false)")
    except Exception as exc:
        record("site_learning", "FAIL", str(exc))
    finally:
        try:
            post_sl("stop=1")
        except Exception:
            pass


def check_csi_diagnostics(ip, auth, timeout):
    """P1 CSI diagnostics contract: decision / health / events / shadow / export
    (Basic auth), plus the import safety guard (slot=active must be rejected)."""
    # decision trace
    try:
        _, body = http_get(ip, "/api/csi/decision", auth=auth, timeout=timeout)
        d = json.loads(body)
        if "reason" in d and "effective_threshold" in d:
            record("csi_decision", "PASS", f"reason={d['reason']} valid={d.get('valid')}")
        else:
            record("csi_decision", "FAIL", "missing reason/effective_threshold")
    except urllib.error.HTTPError as exc:
        record("csi_decision", "WARN" if exc.code in (404, 503) else "FAIL",
               f"HTTP {exc.code} (non-CSI build?)" if exc.code in (404, 503) else f"HTTP {exc.code}")
    except Exception as exc:
        record("csi_decision", "FAIL", str(exc))

    # health reasons + score
    try:
        _, body = http_get(ip, "/api/csi/health", auth=auth, timeout=timeout)
        d = json.loads(body)
        if isinstance(d.get("reasons"), list) and isinstance(d.get("score"), int):
            record("csi_health", "PASS", f"score={d['score']} reasons={d['reasons']}")
        else:
            record("csi_health", "FAIL", "missing score/reasons list")
    except urllib.error.HTTPError as exc:
        record("csi_health", "WARN" if exc.code in (404, 503) else "FAIL", f"HTTP {exc.code}")
    except Exception as exc:
        record("csi_health", "FAIL", str(exc))

    # event ring
    try:
        _, body = http_get(ip, "/api/csi/events?limit=5", auth=auth, timeout=timeout)
        d = json.loads(body)
        if "capacity" in d and isinstance(d.get("events"), list):
            record("csi_events", "PASS", f"count={d.get('count')} cap={d['capacity']} returned={d.get('returned')}")
        else:
            record("csi_events", "FAIL", "missing capacity/events list")
    except urllib.error.HTTPError as exc:
        record("csi_events", "WARN" if exc.code in (404, 503) else "FAIL", f"HTTP {exc.code}")
    except Exception as exc:
        record("csi_events", "FAIL", str(exc))

    # shadow — must be marked diagnostic-only
    try:
        _, body = http_get(ip, "/api/csi/shadow", auth=auth, timeout=timeout)
        d = json.loads(body)
        if d.get("note") == "SHADOW - NO ALARM EFFECT":
            record("csi_shadow", "PASS", f"active={d.get('active')} agree/dis={d.get('agree')}/{d.get('disagree')}")
        else:
            record("csi_shadow", "FAIL", f"missing/incorrect NO-ALARM-EFFECT marker: {d.get('note')!r}")
    except urllib.error.HTTPError as exc:
        record("csi_shadow", "WARN" if exc.code in (404, 503) else "FAIL", f"HTTP {exc.code}")
    except Exception as exc:
        record("csi_shadow", "FAIL", str(exc))

    # export active — must carry an anonymized hash, never a raw MAC / credentials
    try:
        _, body = http_get(ip, "/api/csi/site_model/export?slot=active", auth=auth, timeout=timeout)
        d = json.loads(body)
        leaked = [k for k in ("bssid", "password", "pass", "ssid") if k in d]
        if "bssid_hash" in d and not leaked:
            record("csi_export", "PASS", f"gen={d.get('generation')} bssid_hash present, no raw fields")
        else:
            record("csi_export", "FAIL", f"bssid_hash missing or leaked fields {leaked}")
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            record("csi_export", "WARN", "no active model yet (404)")
        else:
            record("csi_export", "WARN" if exc.code == 503 else "FAIL", f"HTTP {exc.code}")
    except Exception as exc:
        record("csi_export", "FAIL", str(exc))

    # import safety guard — writing the active slot must be refused
    try:
        http_post(ip, "/api/csi/site_model/import?slot=active", auth=auth, timeout=timeout)
        record("csi_import_guard", "FAIL", "import to slot=active was NOT rejected")
    except urllib.error.HTTPError as exc:
        if exc.code == 400:
            record("csi_import_guard", "PASS", "import to slot=active rejected (400)")
        elif exc.code in (404, 503):
            record("csi_import_guard", "WARN", f"HTTP {exc.code} (non-CSI build?)")
        else:
            record("csi_import_guard", "FAIL", f"unexpected HTTP {exc.code} (expected 400)")
    except Exception as exc:
        record("csi_import_guard", "FAIL", str(exc))


def check_metrics(ip, auth, timeout):
    """GET /metrics → Prometheus text containing 'poe2412_' metric prefix."""
    try:
        _, body = http_get(ip, "/metrics", auth=auth, timeout=timeout)
        text = body.decode()
        if "poe2412_" in text:
            record("metrics", "PASS", f"prometheus metrics present ({len(text)} bytes)")
        else:
            record("metrics", "FAIL", "poe2412_ prefix not found in /metrics output")
    except urllib.error.HTTPError as exc:
        if exc.code == 401:
            record("metrics", "WARN", "HTTP 401 (older FW auth variant — non-fatal)")
        else:
            record("metrics", "FAIL", f"HTTP {exc.code}: {exc.reason}")
    except Exception as exc:
        record("metrics", "FAIL", str(exc))


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Smoke test for poe-2412-wifi firmware against a live device.")
    parser.add_argument("--ip", required=True,
                        help="Device IP address (e.g. 192.168.1.50)")
    parser.add_argument("--user", default="admin",
                        help="HTTP Basic auth username (default: admin)")
    parser.add_argument("--password", default="admin",
                        help="HTTP Basic auth password (default: admin)")
    parser.add_argument("--skip-alarm", action="store_true",
                        help="Skip the alarm FSM cycle check")
    parser.add_argument("--skip-learning", action="store_true",
                        help="Skip the CSI site-learning API contract check")
    parser.add_argument("--skip-diagnostics", action="store_true",
                        help="Skip the P1 CSI diagnostics contract checks")
    parser.add_argument("--timeout", type=float, default=8.0,
                        help="Per-request timeout in seconds (default: 8)")
    args = parser.parse_args()

    auth = _make_auth(args.user, args.password)

    print(f"=== poe-2412-wifi smoke test  target={args.ip} ===\n")

    check_version(args.ip, auth, args.timeout)
    check_healthz(args.ip, auth, args.timeout)
    health_data = check_health_core(args.ip, auth, args.timeout)
    check_mqtt(args.ip, auth, args.timeout, health_data)
    check_radar_csi(args.ip, auth, args.timeout, health_data)
    check_gui(args.ip, auth, args.timeout)

    if args.skip_alarm:
        record("alarm", "SKIP", "--skip-alarm flag set")
    else:
        check_alarm(args.ip, auth, args.timeout)

    if args.skip_learning:
        record("site_learning", "SKIP", "--skip-learning flag set")
    else:
        check_site_learning(args.ip, auth, args.timeout)

    if args.skip_diagnostics:
        record("csi_diagnostics", "SKIP", "--skip-diagnostics flag set")
    else:
        check_csi_diagnostics(args.ip, auth, args.timeout)

    check_metrics(args.ip, auth, args.timeout)

    # ── Summary ──────────────────────────────────────────────────────────────
    print()
    counts = {s: 0 for s in ("PASS", "WARN", "FAIL", "SKIP")}
    for _, status, _ in RESULTS:
        counts[status] = counts.get(status, 0) + 1

    print(f"=== SUMMARY  PASS={counts['PASS']}  WARN={counts['WARN']}  "
          f"FAIL={counts['FAIL']}  SKIP={counts['SKIP']} ===")

    if counts["FAIL"]:
        print("\nFailed checks:")
        for name, status, msg in RESULTS:
            if status == "FAIL":
                print(f"  - {name}: {msg}")
        sys.exit(1)

    print("\nAll checks passed (no FAIL).")
    sys.exit(0)


if __name__ == "__main__":
    main()
