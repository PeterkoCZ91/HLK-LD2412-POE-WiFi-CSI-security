#!/usr/bin/env python3
"""Bounded health/heap sampler and optional HTTP/SSE stress test.

Writes JSONL in the requested project directory. No cron or background restart.
Credentials are read from POE_HTTP_USER/POE_HTTP_PASSWORD environment variables.
"""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import threading
import time
import urllib.request

def auth_header():
    pair = f"{os.environ.get('POE_HTTP_USER', 'admin')}:{os.environ.get('POE_HTTP_PASSWORD', 'admin')}"
    return "Basic " + base64.b64encode(pair.encode()).decode()

def fetch(host, path, timeout=8):
    req = urllib.request.Request(f"http://{host}{path}", headers={"Authorization": auth_header()})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.load(response)

def snapshot(host):
    health = fetch(host, "/api/health")
    # Whitelist diagnostics; do not export config, credentials, or network identity.
    keys = ("fw_version", "uptime", "free_heap", "min_heap", "heap", "sse", "web_gate",
            "ota_state", "coredump_present", "chip_temp", "reboot_inhibit", "heap_skips", "csi_data_ok")
    data = {k: health[k] for k in keys if k in health}
    for key in ("mqtt", "ethernet", "csi"):
        block = health.get(key, {})
        allowed = {
            "mqtt": ("connected", "reconnect_total", "publish_fail_total", "publish_fail_streak", "heap_skips"),
            "ethernet": ("flap", "route"),
            "csi": ("active", "data_ok", "starved_ticks", "model_ready", "ml_probability",
                    "ml_motion", "wifi_rssi", "pps")
        }[key]
        data[key] = {k: block[k] for k in allowed if k in block}
    data["utc"] = datetime.now(timezone.utc).isoformat()
    data["watermarks"] = fetch(host, "/api/heap/watermarks")
    return data

def rebooted(data, baseline_uptime, baseline_time):
    return data["uptime"] + 3 < baseline_uptime + time.monotonic() - baseline_time

def stress(host, streams, concurrency, seconds, baseline_uptime, baseline_time):
    stop = threading.Event()
    results = {"http_ok": 0, "http_refused": 0, "sse_opened": 0, "sse_events": 0,
               "sse_errors": 0, "reboot_detected": False}
    lock = threading.Lock()
    def sse():
        req = urllib.request.Request(f"http://{host}/events", headers={
            "Authorization": auth_header(), "Accept": "text/event-stream"})
        try:
            with urllib.request.urlopen(req, timeout=8) as response:
                with lock: results["sse_opened"] += 1
                while not stop.is_set():
                    line = response.readline()
                    if not line: break
                    if line.startswith(b"data:"):
                        with lock: results["sse_events"] += 1
        except Exception:
            with lock: results["sse_errors"] += 1
    def http():
        if stop.is_set(): return
        try:
            data = fetch(host, "/api/health")
            ok = "uptime" in data
            if ok and rebooted(data, baseline_uptime, baseline_time):
                with lock: results["reboot_detected"] = True
                stop.set()
        except Exception:
            ok = False
        with lock: results["http_ok" if ok else "http_refused"] += 1
    threads = [threading.Thread(target=sse) for _ in range(streams)]
    for thread in threads: thread.start()
    deadline = time.monotonic() + seconds
    try:
        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            while time.monotonic() < deadline and not stop.is_set():
                list(pool.map(lambda _: http(), range(concurrency)))
                stop.wait(0.5)
    finally:
        stop.set()
        for thread in threads: thread.join(timeout=10)
    return results

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--duration", type=float, default=600)
    p.add_argument("--interval", type=float, default=15)
    p.add_argument("--stress", action="store_true")
    p.add_argument("--sse", type=int, default=2)
    p.add_argument("--concurrency", type=int, default=6)
    args = p.parse_args()
    if args.duration <= 0 or args.interval < 1 or not 1 <= args.concurrency <= 16 or not 0 <= args.sse <= 4:
        p.error("invalid duration, interval, concurrency or SSE count")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents accidentally mixing builds/runs.
    with args.output.open("x") as out:
        def emit(data):
            out.write(json.dumps(data, allow_nan=False) + "\n")
            out.flush()
            if data.get("phase") == "sample":
                print(json.dumps({k: data.get(k) for k in ("phase", "uptime", "free_heap", "min_heap", "failure")}), flush=True)
            else:
                print(json.dumps(data), flush=True)
        baseline = snapshot(args.host)
        baseline_time = time.monotonic()
        emit({"phase": "baseline", **baseline})
        stress_failures = 0
        if args.stress:
            result = stress(args.host, args.sse, args.concurrency, min(args.duration, 120),
                            baseline["uptime"], baseline_time)
            stress_failures = int(result["reboot_detected"] or result["http_ok"] == 0
                                  or result["sse_opened"] != args.sse
                                  or (args.sse > 0 and result["sse_events"] == 0)
                                  or result["sse_errors"] > 0)
            emit({"phase": "stress", "failure": bool(stress_failures), **result})
            time.sleep(15)  # allow admission TTL and TCP buffers to drain
        deadline = time.monotonic() + args.duration
        failures, samples, minimum = stress_failures, 0, baseline.get("free_heap", 0)
        while True:
            try:
                data = snapshot(args.host)
                reboot = rebooted(data, baseline["uptime"], baseline_time)
                bad = (reboot or not data.get("mqtt", {}).get("connected", False)
                       or data.get("csi_data_ok") is False or data["free_heap"] < 14000
                       or data["fw_version"] != baseline["fw_version"])
                failures += int(bad)
                samples += 1
                minimum = min(minimum, data["free_heap"])
                emit({"phase": "sample", "failure": bad, **data})
            except Exception as exc:
                failures += 1
                emit({"phase": "sample", "failure": True, "error_type": type(exc).__name__})
            remaining = deadline - time.monotonic()
            if remaining <= 0: break
            time.sleep(min(args.interval, remaining))
        emit({"phase": "summary", "samples": samples, "failures": failures, "min_polled_heap": minimum})
    return int(failures != 0)

if __name__ == "__main__":
    raise SystemExit(main())

