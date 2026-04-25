#!/usr/bin/env python3
"""
Push ESP health and CSI metrics to Home Assistant via the REST states API.

Typical usage:
  python3 tools/ha_csi_push.py
  python3 tools/ha_csi_push.py --dry-run
  python3 tools/ha_csi_push.py daemon --interval 60

Configuration can be overridden by environment variables:
  ESP_BASE_URL, ESP_USER, ESP_PASS, HA_URL, HA_TOKEN, PUSH_INTERVAL, HTTP_TIMEOUT
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from dataclasses import dataclass
from typing import Any, Dict, Iterable, Optional

import requests
from requests.auth import HTTPDigestAuth


DEFAULT_ESP_BASE_URL = os.getenv("ESP_BASE_URL", "http://your-esp.local")
DEFAULT_ESP_USER = os.getenv("ESP_USER", "admin")
DEFAULT_ESP_PASS = os.getenv("ESP_PASS", "admin")

DEFAULT_HA_URL = os.getenv("HA_URL", "http://your-ha.local:8123")
DEFAULT_HA_TOKEN = os.getenv("HA_TOKEN", "")

DEFAULT_INTERVAL = int(os.getenv("PUSH_INTERVAL", "300"))
DEFAULT_TIMEOUT = float(os.getenv("HTTP_TIMEOUT", "10"))
DEFAULT_RETRIES = int(os.getenv("HTTP_RETRIES", "2"))


@dataclass(frozen=True)
class SensorSpec:
    entity_id: str
    endpoint: str
    path: str
    friendly_name: str
    unit: Optional[str] = None
    icon: Optional[str] = None
    device_class: Optional[str] = None
    state_class: Optional[str] = None


SENSORS = [
    SensorSpec(
        "sensor.poe2412_csi_health",
        "health",
        "health_score",
        "CSI Health Score",
        "%",
        icon="mdi:heart-pulse",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_uptime",
        "health",
        "uptime",
        "CSI Uptime",
        "s",
        icon="mdi:timer-outline",
        device_class="duration",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_frame_rate",
        "health",
        "frame_rate",
        "CSI Frame Rate",
        "fps",
        icon="mdi:sine-wave",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_chip_temp",
        "health",
        "chip_temp",
        "CSI Chip Temperature",
        "°C",
        icon="mdi:thermometer",
        device_class="temperature",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_wifi_rssi",
        "health",
        "csi.wifi_rssi",
        "CSI WiFi RSSI",
        "dBm",
        icon="mdi:wifi",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_rssi_low_snr",
        "health",
        "csi.rssi_low_snr",
        "CSI RSSI Low SNR",
        icon="mdi:wifi-alert",
    ),
    SensorSpec(
        "sensor.poe2412_csi_rssi_too_hot",
        "health",
        "csi.rssi_too_hot",
        "CSI RSSI Too Hot",
        icon="mdi:thermometer-alert",
    ),
    SensorSpec(
        "sensor.poe2412_csi_stuck_motion",
        "health",
        "csi.stuck_motion_count",
        "CSI Stuck Motion Count",
        icon="mdi:motion-sensor-off",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_stuck_raises",
        "health",
        "csi.stuck_raise_count",
        "CSI Stuck Raise Count",
        icon="mdi:chart-line-variant",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_base_threshold",
        "health",
        "csi.base_threshold",
        "CSI Base Threshold",
        icon="mdi:tune-variant",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_bssid_changes",
        "health",
        "csi.bssid_change_count",
        "CSI BSSID Changes",
        icon="mdi:wifi-cog",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_adaptive_enabled",
        "health",
        "csi.adaptive_enabled",
        "CSI Adaptive Enabled",
        icon="mdi:auto-fix",
    ),
    SensorSpec(
        "sensor.poe2412_csi_adaptive_th",
        "health",
        "csi.adaptive_threshold",
        "CSI Adaptive Threshold",
        icon="mdi:tune",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_effective_th",
        "health",
        "csi.effective_threshold",
        "CSI Effective Threshold",
        icon="mdi:tune-vertical",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_p95_samples",
        "health",
        "csi.p95_samples",
        "CSI P95 Samples",
        "samples",
        icon="mdi:chart-bell-curve",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_enabled",
        "health",
        "csi.nbvi_enabled",
        "CSI NBVI Enabled",
        icon="mdi:waves-arrow-up",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_ready",
        "health",
        "csi.nbvi_ready",
        "CSI NBVI Ready",
        icon="mdi:check-decagram",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_mask",
        "health",
        "csi.nbvi_mask",
        "CSI NBVI Mask",
        icon="mdi:grid",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_active",
        "health",
        "csi.nbvi_active",
        "CSI NBVI Active",
        icon="mdi:vector-selection",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_samples",
        "health",
        "csi.nbvi_samples",
        "CSI NBVI Samples",
        icon="mdi:counter",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_recalc_count",
        "health",
        "csi.nbvi_recalc_count",
        "CSI NBVI Recalc Count",
        icon="mdi:refresh",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_best_sc",
        "health",
        "csi.nbvi_best_sc",
        "CSI NBVI Best SC",
        icon="mdi:medal",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_worst_sc",
        "health",
        "csi.nbvi_worst_sc",
        "CSI NBVI Worst SC",
        icon="mdi:alert-circle-outline",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_best_cv",
        "health",
        "csi.nbvi_best_cv",
        "CSI NBVI Best CV",
        icon="mdi:chart-line",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_nbvi_worst_cv",
        "health",
        "csi.nbvi_worst_cv",
        "CSI NBVI Worst CV",
        icon="mdi:chart-line-variant",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_active",
        "health",
        "csi.learning_active",
        "CSI Site Learning Active",
        icon="mdi:school",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_progress",
        "health",
        "csi.learning_progress",
        "CSI Site Learning Progress",
        "%",
        icon="mdi:progress-clock",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_elapsed",
        "health",
        "csi.learning_elapsed_s",
        "CSI Site Learning Elapsed",
        "s",
        icon="mdi:timer-sand",
        device_class="duration",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_duration",
        "health",
        "csi.learning_duration_s",
        "CSI Site Learning Duration",
        "s",
        icon="mdi:clock-outline",
        device_class="duration",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_samples",
        "health",
        "csi.learning_samples",
        "CSI Site Learning Samples",
        icon="mdi:counter",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_rejected",
        "health",
        "csi.learning_rejected_motion",
        "CSI Site Learning Rejected Motion",
        icon="mdi:motion-sensor-off",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learning_th_estimate",
        "health",
        "csi.learning_threshold_estimate",
        "CSI Site Learning Threshold Estimate",
        icon="mdi:tune",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_model_ready",
        "health",
        "csi.model_ready",
        "CSI Site Model Ready",
        icon="mdi:brain",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learned_threshold",
        "health",
        "csi.learned_threshold",
        "CSI Learned Threshold",
        icon="mdi:tune-variant",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learned_mean_variance",
        "health",
        "csi.learned_mean_variance",
        "CSI Learned Mean Variance",
        icon="mdi:chart-bell-curve",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learned_std_variance",
        "health",
        "csi.learned_std_variance",
        "CSI Learned Std Variance",
        icon="mdi:sigma",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learned_max_variance",
        "health",
        "csi.learned_max_variance",
        "CSI Learned Max Variance",
        icon="mdi:chart-line",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_learned_samples",
        "health",
        "csi.learned_samples",
        "CSI Learned Samples",
        icon="mdi:database-check",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_motion",
        "csi",
        "motion",
        "CSI Motion",
        icon="mdi:motion-sensor",
    ),
    SensorSpec(
        "sensor.poe2412_csi_composite",
        "csi",
        "composite",
        "CSI Composite",
        icon="mdi:chart-areaspline",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_turbulence",
        "csi",
        "turbulence",
        "CSI Turbulence",
        icon="mdi:waves",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_phase_turb",
        "csi",
        "phase_turb",
        "CSI Phase Turbulence",
        icon="mdi:waveform",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_ratio_turb",
        "csi",
        "ratio_turb",
        "CSI Ratio Turbulence",
        icon="mdi:approximate",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_breathing",
        "csi",
        "breathing",
        "CSI Breathing Score",
        icon="mdi:lungs",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_dser",
        "csi",
        "dser",
        "CSI DSER",
        icon="mdi:chart-timeline-variant",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_plcr",
        "csi",
        "plcr",
        "CSI PLCR",
        icon="mdi:chart-histogram",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_variance",
        "csi",
        "variance",
        "CSI Variance",
        icon="mdi:sigma",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_packets",
        "csi",
        "packets",
        "CSI Packet Count",
        "packets",
        icon="mdi:counter",
        state_class="total_increasing",
    ),
    SensorSpec(
        "sensor.poe2412_csi_pps",
        "csi",
        "pps",
        "CSI Packets Per Second",
        "pps",
        icon="mdi:speedometer",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_idle_ready",
        "csi",
        "idle_ready",
        "CSI Idle Ready",
        icon="mdi:power-sleep",
    ),
    SensorSpec(
        "sensor.poe2412_csi_calibrating",
        "csi",
        "calibrating",
        "CSI Calibrating",
        icon="mdi:progress-clock",
    ),
    SensorSpec(
        "sensor.poe2412_csi_calib_pct",
        "csi",
        "calib_pct",
        "CSI Calibration Progress",
        "%",
        icon="mdi:progress-check",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_fusion_presence",
        "csi",
        "fusion.presence",
        "CSI Fusion Presence",
        icon="mdi:account-search",
    ),
    SensorSpec(
        "sensor.poe2412_csi_fusion_confidence",
        "csi",
        "fusion.confidence",
        "CSI Fusion Confidence",
        "%",
        icon="mdi:shield-check",
        state_class="measurement",
    ),
    SensorSpec(
        "sensor.poe2412_csi_fusion_source",
        "csi",
        "fusion.source",
        "CSI Fusion Source",
        icon="mdi:source-branch",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Push ESP CSI/health metrics to Home Assistant."
    )
    parser.add_argument(
        "command",
        nargs="?",
        choices=("once", "daemon"),
        default="once",
        help="Run once or keep pushing in a loop",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=DEFAULT_INTERVAL,
        help=f"Seconds between pushes in daemon mode (default: {DEFAULT_INTERVAL})",
    )

    parser.add_argument(
        "--esp-base-url",
        default=DEFAULT_ESP_BASE_URL,
        help=f"ESP base URL (default: {DEFAULT_ESP_BASE_URL})",
    )
    parser.add_argument(
        "--esp-user",
        default=DEFAULT_ESP_USER,
        help=f"ESP username (default: {DEFAULT_ESP_USER})",
    )
    parser.add_argument(
        "--esp-pass",
        default=DEFAULT_ESP_PASS,
        help="ESP password",
    )
    parser.add_argument(
        "--ha-url",
        default=DEFAULT_HA_URL,
        help=f"Home Assistant base URL (default: {DEFAULT_HA_URL})",
    )
    parser.add_argument(
        "--ha-token",
        default=DEFAULT_HA_TOKEN,
        help="Home Assistant long-lived access token",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help=f"HTTP timeout in seconds (default: {DEFAULT_TIMEOUT})",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=DEFAULT_RETRIES,
        help=f"Retries for transient ESP/API errors (default: {DEFAULT_RETRIES})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Read and print values, do not push anything to Home Assistant",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print every pushed or skipped sensor",
    )

    args = parser.parse_args()
    if not args.ha_token and not args.dry_run:
        parser.error("HA token is required unless --dry-run is used.")
    return args


def endpoint_urls(base_url: str) -> Dict[str, str]:
    root = base_url.rstrip("/")
    return {
        "health": f"{root}/api/health",
        "csi": f"{root}/api/csi",
    }


def get_nested(data: Dict[str, Any], path: str) -> Any:
    value: Any = data
    for key in path.split("."):
        if isinstance(value, dict) and key in value:
            value = value[key]
        else:
            return None
    return value


def normalize_state(value: Any) -> Any:
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, float):
        return round(value, 6)
    return value


def fetch_json(
    session: requests.Session,
    url: str,
    auth: HTTPDigestAuth,
    timeout: float,
    retries: int,
) -> Optional[Dict[str, Any]]:
    last_error: Optional[Exception] = None

    for attempt in range(1, retries + 2):
        try:
            response = session.get(url, auth=auth, timeout=timeout)
            response.raise_for_status()
            return response.json()
        except Exception as exc:
            last_error = exc
            if attempt <= retries:
                time.sleep(0.5)

    print(f"ESP fetch error for {url}: {last_error}", file=sys.stderr)
    return None


def fetch_payloads(
    session: requests.Session,
    base_url: str,
    user: str,
    password: str,
    timeout: float,
    retries: int,
) -> Dict[str, Dict[str, Any]]:
    auth = HTTPDigestAuth(user, password)
    payloads: Dict[str, Dict[str, Any]] = {}

    for endpoint, url in endpoint_urls(base_url).items():
        payload = fetch_json(session, url, auth, timeout, retries)
        if payload is not None:
            payloads[endpoint] = payload

    return payloads


def build_attributes(spec: SensorSpec, raw_value: Any) -> Dict[str, Any]:
    attrs: Dict[str, Any] = {
        "friendly_name": f"POE2412 {spec.friendly_name}",
        "source_endpoint": spec.endpoint,
        "source_path": spec.path,
    }
    if spec.unit:
        attrs["unit_of_measurement"] = spec.unit
    if spec.icon:
        attrs["icon"] = spec.icon
    if spec.device_class:
        attrs["device_class"] = spec.device_class
    if spec.state_class:
        attrs["state_class"] = spec.state_class
    if isinstance(raw_value, bool):
        attrs["value_text"] = "true" if raw_value else "false"
    return attrs


def push_ha(
    session: requests.Session,
    ha_url: str,
    ha_token: str,
    spec: SensorSpec,
    raw_value: Any,
    timeout: float,
    dry_run: bool = False,
) -> bool:
    state = normalize_state(raw_value)
    attrs = build_attributes(spec, raw_value)

    if dry_run:
        print(f"DRY {spec.entity_id} = {state} ({spec.path})")
        return True

    headers = {
        "Authorization": f"Bearer {ha_token}",
        "Content-Type": "application/json",
    }
    payload = {"state": str(state), "attributes": attrs}

    try:
        response = session.post(
            f"{ha_url.rstrip('/')}/api/states/{spec.entity_id}",
            json=payload,
            headers=headers,
            timeout=timeout,
        )
        if response.status_code in (200, 201):
            return True

        print(
            f"HA push error for {spec.entity_id}: HTTP {response.status_code} {response.text}",
            file=sys.stderr,
        )
        return False
    except Exception as exc:
        print(f"HA push error for {spec.entity_id}: {exc}", file=sys.stderr)
        return False


def iter_sensor_values(
    payloads: Dict[str, Dict[str, Any]],
    specs: Iterable[SensorSpec],
) -> Iterable[tuple[SensorSpec, Any]]:
    for spec in specs:
        payload = payloads.get(spec.endpoint)
        if not payload:
            continue

        value = get_nested(payload, spec.path)
        if value is not None:
            yield spec, value


def run_once(args: argparse.Namespace) -> bool:
    session = requests.Session()
    payloads = fetch_payloads(
        session=session,
        base_url=args.esp_base_url,
        user=args.esp_user,
        password=args.esp_pass,
        timeout=args.timeout,
        retries=args.retries,
    )

    if not payloads:
        print("No ESP data fetched.", file=sys.stderr)
        return False

    success = 0
    seen = 0

    for spec, value in iter_sensor_values(payloads, SENSORS):
        seen += 1
        ok = push_ha(
            session=session,
            ha_url=args.ha_url,
            ha_token=args.ha_token,
            spec=spec,
            raw_value=value,
            timeout=args.timeout,
            dry_run=args.dry_run,
        )
        if ok:
            success += 1
            if args.verbose and not args.dry_run:
                print(f"OK  {spec.entity_id} = {normalize_state(value)}")
        elif args.verbose:
            print(f"ERR {spec.entity_id} ({spec.path})", file=sys.stderr)

    missing = len(SENSORS) - seen
    mode = "dry-run" if args.dry_run else "push"
    print(f"Finished {mode}: pushed {success}/{seen} sensors, missing {missing}")
    return success > 0


def daemon(args: argparse.Namespace) -> int:
    interval = args.interval
    print(f"Starting daemon, interval={interval}s")

    while True:
        run_once(args)
        time.sleep(interval)


def main() -> int:
    args = parse_args()
    if args.command == "daemon":
        return daemon(args)
    return 0 if run_once(args) else 1


if __name__ == "__main__":
    raise SystemExit(main())
