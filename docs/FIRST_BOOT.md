# First-Boot & Sensor Verification

This guide covers getting the firmware onto a device and confirming each sensor
actually produces data. Radar and CSI are **independent** — the node is fully
functional with only one of them wired (e.g. CSI-only), and the fusion alarm
simply uses whatever signals are present.

## About the prebuilt release binary

`firmware-vX.Y.Z-poe-wifi-csi.bin` (attached to the GitHub release) is:

- The **CSI build** (`esp32_poe_csi`) application image.
- Built with the **placeholder** `secrets.h` / `known_devices.h` — no WiFi SSID,
  MQTT broker, credentials, or device identity are baked in. You set those at
  runtime from the dashboard; they live in NVS.
- An **OTA app-image only** — it does *not* contain the bootloader, partition
  table, or `boot_app0`, so it cannot be written to a blank board with a single
  `esptool write_flash`. Two correct ways to use it:

| Goal | Method |
|------|--------|
| **Update a device already running this firmware** | Apply this asset over OTA (Pull OTA / espota). See [OTA_OPERATIONS.md](OTA_OPERATIONS.md). |
| **First flash of a brand-new board** | Build from source over USB — PlatformIO writes bootloader + partitions + app together. See the [Quick Start](../README.md#quick-start). |

## Option A — apply over OTA (existing device)

```bash
tools/pull_ota_deploy.sh \
  --host DEVICE_IP \
  --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
  --expect-mac EXPECTED_MAC \
  --expect-id EXPECTED_MQTT_ID \
  --cold-reboot --flash
```

Dry-run first (omit `--flash`). Full details and the espota fallback are in
[OTA_OPERATIONS.md](OTA_OPERATIONS.md).

## Option B — first USB flash (new board)

```bash
cp include/secrets.h.example include/secrets.h
cp include/known_devices.h.example include/known_devices.h
# edit secrets.h: CSI WiFi SSID/pass, MQTT broker (optional)
pio run -e esp32_poe_csi --target upload
```

## First boot

1. The device gets a DHCP lease over Ethernet. Find its IP in your router's DHCP
   table or the USB serial console (115200 baud).
2. Open `http://DEVICE_IP`. Default login is `admin` / `admin`.
3. **Change the admin password immediately** (Network & Cloud tab). Until you do,
   `/api/health` reports `is_default_pass: true` and the dashboard blocks ARM.
4. With the prebuilt binary's placeholder config, CSI WiFi and MQTT are **not**
   connected yet — that is expected. Configure them below.

## Verify the mmWave radar (HLK-LD2412)

`GET /api/telemetry` (HTTP **Basic** auth) is the radar's live output. Healthy
reference values:

| Field | Healthy | If wrong |
|-------|---------|----------|
| `uart_state` | `RUNNING` | `DISCONNECTED` → check wiring: radar **TX → GPIO33**, **RX → GPIO32** (crossover), common **GND**, UART **256000** bps |
| `connected` | `true` | `false` → UART not talking to the module |
| `frame_rate` | `> 0` (frames/s) | `0` → wrong pins or baud rate |
| `health_score` | `~100` | low → frame errors / timeouts / checksum errors (see the `*_count`/`*_errors` fields) |
| `state` | `idle` ↔ `motion` | should flip to `motion` when you move in front |
| `distance_mm` | tracks the target | stays `0` → no detections reaching the parser |
| `moving_energy` / `static_energy` | rise on motion / stationary presence | |
| `moving_gate` / `static_gate` | active gate index (0–13) | |
| `motion_type`, `motion_dir`, `light`, `tamper` | context fields | |

**Quick functional check:** wave a hand 1–3 m in front of the radar → `state`
goes `motion`, `moving_energy` climbs, `distance_mm` tracks you.

- **Engineering mode** — `POST /api/engineering` adds raw per-gate arrays
  (`gate_move` / `gate_still`) to the telemetry for sensitivity tuning.
- **Static reflector learning** — `POST /api/radar/learn-static` (3 min) on first
  install to auto-suggest ignore zones for furniture/walls.
- The same `uart_state` / `frame_rate` / `health_score` / `resolution` are also
  mirrored in `/api/health`.

> An all-zero radar with `uart_state: DISCONNECTED` is almost always wiring or
> baud — it does **not** affect CSI. The node keeps running CSI-only.

> **Radar-less (CSI-only) units.** If no radar is wired, the node logs the
> radar status **once** after ~30 s, then latches radar monitoring off and runs
> on CSI detection alone — it does *not* spam "Radar sensor connection lost".
> The radar cannot be (re)attached without a reboot, so monitoring stays off
> until restart. Watch this state via `/api/health` →
> `radar_monitoring_disabled: true` or the `poe2412_radar_monitoring_disabled`
> Prometheus metric.

## Verify WiFi CSI

1. Point the ESP at a **2.4 GHz** AP: CSI tab → *WiFi*, or
   `POST /api/csi/wifi` (NVS-persisted, requires reboot).
2. After it reconnects, read `/api/csi` or the `csi` block in `/api/health`:

| Field | Healthy | Meaning |
|-------|---------|---------|
| `wifi_rssi` | −40 … −75 dBm | link quality to the AP |
| `ht_ltf_seen` | `true` | the AP emits the 802.11n HT-LTF frames CSI needs (AP compatibility) |
| `pps` / packets | `> 0` | CSI frames are arriving; `0` with `ht_ltf_seen=true` → AP isolation / band-steering |
| `active` | `true` | CSI pipeline is running |
| `threshold` / `effective_threshold` | set | current motion threshold |

If `ht_ltf_seen` stays `false` with healthy RSSI, or `pps ≈ 0`, see
[AP requirements](../README.md#wifi-access-point-requirements-for-csi).

3. Tune for low false positives: `POST /api/csi/calibrate` (quiet room, ~30 s),
   then `POST /api/csi/site_learning?duration_s=86400` (24 h recommended; stop
   early with `?stop=1`). Only run site learning while the space is empty.
   See [Site Learning](../README.md#site-learning-workflow-csi).

## Verify MQTT / Home Assistant (optional)

1. Network & Cloud tab → MQTT broker, or `POST /api/mqtt/config`.
2. Check `/api/health` → `mqtt.connected: true`, `mqtt.publish_fail_streak: 0`.
3. HA auto-discovery publishes presence, CSI motion, distance, energy, alarm
   state, and fusion-source entities (see the [FAQ](../README.md#faq)).

## Healthy baseline cheat-sheet

| Signal | Where | Green light |
|--------|-------|-------------|
| Radar UART | `/api/telemetry` | `uart_state: RUNNING`, `frame_rate > 0`, `health_score ~100` |
| Radar detection | `/api/telemetry` | `state` flips to `motion` on movement |
| CSI link | `/api/csi` | `wifi_rssi` −40…−75 dBm, `ht_ltf_seen: true` |
| CSI data | `/api/csi` | `pps > 0`, `active: true` |
| MQTT | `/api/health` | `mqtt.connected: true`, `publish_fail_streak: 0` |
| Security | `/api/health` | `is_default_pass: false` (admin password changed) |
