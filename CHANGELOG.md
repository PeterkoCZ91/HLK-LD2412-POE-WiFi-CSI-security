# Changelog

All notable changes to this project will be documented in this file.

## [5.0.0-poe-wifi] - 2026-04-25

Major release consolidating four months of CSI work, radar fusion improvements, and operational hardening.

### Added — CSI presence detection

- **WiFi CSI motion detection** — sniffer mode pulls Channel State Information from frames between the configured AP and ESP, exposes per-packet variance / turbulence / phase-turbulence / breathing / DSER / PLCR features, and contributes presence + confidence into the fusion engine. Configured via `/api/csi/wifi` (SSID/pass) or `secrets.h` build-time defaults.
- **Site learning** — quiet-room baseline learner samples 30 min – 168 h, persists `mean / std / max variance` and a derived threshold to NVS, and refreshes the model continuously via EMA so it adapts to slow environmental drift. Full GUI controls (start/stop/progress/elapsed) on tab 6.
- **MLP motion classifier** — 17-feature shallow neural network (DSER, PLCR, variance, phase-turbulence, breathing, …) compiled into the firmware (`include/services/ml_features.h` + `ml_weights.h`). Acts as third detection signal in the fusion path; F1 = 0.852 on the validation dataset.
- **3-way fusion** — radar + CSI variance + ML probability combined into a single `fusion_source` field surfaced over MQTT and `/api/csi`. ML serves as tiebreaker when the two physical signals disagree; cannot trigger an alarm by itself in `refl_auto` zones (kinetic floor still required).
- **NBVI subcarrier auto-selection** — every 500 packets the service ranks the 12 subcarriers by coefficient of variation and keeps the K=8 most stable. Drops noisy SCs without user tuning. Exposes `nbvi_*` diagnostics.
- **Adaptive P95 threshold** — 300-sample rolling buffer raises the variance threshold by 1.1× when ambient noise spikes (only raises, never lowers) to suppress AP-induced false positives.
- **Stuck-motion auto-raise** — if continuous motion lasts > 24 h, threshold is multiplied by 1.5× (max 3 raises per boot) and reset on next quiet window.
- **BSSID-change baseline reset** — AP roam invalidates the learned model so the next quiet period rebuilds it cleanly.
- **AP compatibility probe** — `ht_ltf_seen` flag in `/api/csi` lets HA operators verify the AP actually emits the HT_LTF frames CSI needs. Documented known-working / known-failing AP list in README.

### Added — operational

- **Pull-based OTA endpoint** — `POST /api/update/pull` accepts a JSON body with a firmware URL, fetches the image with `HTTPClient` + `httpUpdate`, and persists phase/error to NVS. `/api/update/pull/status` exposes progress. Robust against HTTP/HTTPS, follows redirects, freezes MQTT + radar tasks during the swap.
- **Cold-reboot-before-OTA GUI flow** — default-on checkbox in the upload form posts `/api/restart`, polls `/api/health` until uptime < 30 s, then uploads. Eliminates the second-OTA-in-boot stall on this hardware (3/3 success in bench, was 1/3 without).
- **Schedule arm/disarm** — daily auto-arm and auto-disarm times configurable in the GUI; `auto_arm_minutes` delay supports staged morning routines.
- **Network tab** — static-IP / DHCP / DNS configuration with validation.
- **Timezone picker** — IANA TZ string + DST offset stored in NVS.
- **Config export / import** — full `/api/config/export` snapshot includes radar, alarm, MQTT, CSI, Telegram, schedule, network, TZ. JSON file roundtrips through `/api/config/import`.
- **Config snapshot before OTA** — backup written to NVS so the operator can restore on rollback.
- **Heartbeat MQTT topic** — dedicated `<prefix>/heartbeat` with uptime payload defeats Home Assistant deduplication of identical messages.
- **Site-learning persistence** — learned site model survives OTA via NVS.
- **Telegram test endpoint** — `POST /api/telegram/test` sends a probe message so the operator can verify token/chat_id without arming.

### Added — repo / project

- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `.github/ISSUE_TEMPLATE/*`, `.github/pull_request_template.md`, `.github/workflows/build.yml` (CI build).
- `docs/MAINTAINING.md`, `docs/RELEASE_CHECKLIST.md`.
- `tools/ha_csi_push.py` — REST-state push helper for sites that prefer pull-from-HA over MQTT.
- README section: "WiFi Access Point Requirements for CSI" with known-working / known-failing AP list.

### Changed

- All JSON `GET` and `POST`-response handlers in `src/WebRoutes.cpp` now stream directly to TCP via `AsyncResponseStream` (was: build into `String`, then `request->send`). Eliminates the transient heap-spike that produced sustained `RemoteDisconnected` / empty-reply failures under continuous polling on weak-RSSI deployments.
- `LD2412_Extended` library now exposes `setBluetooth()`, `readMacAddress()`, and the renamed DBC API.
- Radar Bluetooth disabled by default on boot (security: prevents HLK app pairing without explicit operator action).
- Reconnect-replay invalidation extended to TIER 3 telemetry (uart_state, frame_rate, heap, …) so deadband-gated values re-fire on next publish after MQTT reconnect.
- 5-min heartbeat republish of TIER 3 diagnostics so HA `last_reported` cannot freeze on healthy-but-stable values.

### Fixed

- **MQTT recovery** — fail-streak counter, soft-recovery on N consecutive publish failures, cache invalidation on reconnect.
- **DMS restart loop** — overflow guard in `(now - lastPublish)` after ~49 days uptime; `_lastPublish` reset on MQTT connect.
- **HTTP OTA stall** — first-OTA-after-cold-boot succeeds reliably; subsequent OTAs in the same boot session require the GUI cold-reboot step (root cause is in the AsyncTCP / heap layer, not the Update library — workaround documented).
- **Static-zone false alarms** — sticky static-filter (5-frame clear) prevents short move-energy spikes in `refl_auto` zones from promoting to PENDING; `_isStaticFiltered` guard added to `radarQualifies`.
- **CSI fusion source buffer** — short-window buffer prevents `fusion_source` from flapping on the boundary frame between radar-only and ML-only branches.
- **i18n** — fixed 7 broken Czech strings in the GUI translation table.
- **Out-of-range radar gate styling** — gate visualisation now clamps to the configured display range.
- **Learning progress label** — shows elapsed / target rather than just percentage.

### Deprecated / removed

- ESPAsyncWiFiManager removed (Ethernet is the only management transport on this board).

### Bench numbers

- RAM: 20.6 % (67 372 / 327 680 B)
- Flash: 36.6 % (1 536 885 / 4 194 304 B)
- HTTP failure rate under sustained polling: < 1 % (was 94 % on production deployment with weak RSSI before the streaming fix)

### Migration notes

- Existing v4.5.x deployments can OTA directly to v5.0.0.
- Site-learning model persists across the upgrade — no need to re-learn unless you moved the sensor.
- If your AP is not in the known-working list, watch `/api/csi` for `ht_ltf_seen=true` after first boot. If it stays `false`, see the README troubleshooting row.
- New defaults are conservative; review the schedule, network, and TZ tabs after upgrade.

## [4.5.5-poe-wifi] - 2026-04-20

### Fixed
- **Engineering Mode initial state** — Home Assistant showed `Engineering Mode` as `Unknown` until it was toggled, because the change-gated MQTT publish never fired when boot state matched the zero-initialized cache. Added `lastPub.eng_mode` to the reconnect-replay invalidation block so the first post-connect pass always publishes the real state.

## [4.5.4-poe-wifi] - 2026-04-19

### Added
- **DSER (Dynamic-to-Static Energy Ratio)** — per-packet CSI feature from Uni-Fi paper (arXiv 2601.10980): `log(|H_d|²/|H_s|²)` averaged across 12 selected subcarriers, with slow EMA (α=0.01, ~100-packet time constant) tracking the static component. Negative in absence (-6..-4), rises toward 0 with motion. Published as `<prefix>/dser` and exposed via `/api/csi/status` + SSE telemetry.
- **PLCR (Path-Length Change Rate proxy)** — RMS of unwrapped inter-packet phase delta divided by 2π. Per-SC phase delta wrapped to `[-π,π]`. ~0 at rest, 0.3-0.5 during walking. Published as `<prefix>/plcr`.
- **RSSI health check** — diagnostic warnings when RSSI >-40 dBm (near-AP saturation) or <-70 dBm (low SNR). Reuses existing 30s throttle in `CSIService::update()`.

### Changed
- `resetIdleBaseline()` now clears `_csiStatic`, `_csiPhasePrev`, and the feature convergence state — takes ~200 packets (~2s) to stabilize after reset.

## [4.5.3-poe-wifi] - 2026-04-18

### Fixed
- **OTA Digest re-auth stall at 64KB** — auth now runs once on first chunk and is tracked via static `otaAuthorized` flag; previously AsyncWebServer re-validated Digest auth on each chunk, causing ETH connection drop on LAN8720A after 64KB buffer
- **OTA commit guard** — `Update.end(true)` now runs only when `Update.hasError()` is false; on error path `Update.abort()` is called to discard partial image instead of potentially committing a corrupted firmware
- **OTA auth flag reset** — `otaAuthorized` cleared on `final` chunk (success or error) so stale auth state cannot leak into subsequent uploads
- **Web body upload bounds check** — `/api/zones`, `/api/config/import`, and `/api/security/event/ack` body handlers now verify `index + len <= total` before `memcpy()` to prevent heap overflow if a malformed client sends more data than declared `total`

## [4.5.1-poe-wifi] - 2026-04-18

### Added
- **Fusion → alarm** — CSI-only presence can trigger ARMED→PENDING→TRIGGERED; radar false positives suppressed when CSI disagrees (fusion moved before alarm logic)
- **Auto-zones from learning** — `POST /api/radar/apply-learn` creates ignore_static_only zone from reflector learn results with overlap detection
- **Event timeline UI** — 24h density heatmap, type filtering, pagination, CSV export button
- **CZ/EN language toggle** — i18n dictionary with `t()` helper and `data-i18n` attributes; language persisted in localStorage; eliminates need for separate repo copies
- **Traffic generator tuning** — configurable target port (`traffic_port`), ICMP ping mode (`traffic_icmp`), PPS rate (`traffic_pps`) via `/api/csi` POST; GUI controls in CSI tab
- **Multi-sensor mesh verification** — MQTT-based peer alarm cross-validation with 5s confirm window
- **Supervision heartbeat** — 60s peer alive publish, 3min offline alert with tamper notification
- **GUI screenshots** — docs/screenshots/ with anonymized dashboard captures

### Fixed
- **DMS millis() overflow** — after ~49.7 days uptime, `(now - _lastPublish)` wraps to UINT32_MAX causing infinite MQTT reconnect loop; added overflow guard (ignore age > 30 days) and reset `_lastPublish` on MQTT connect
- **OTA delay** — 500ms delay before reboot so HTTP response passes through nginx proxy (fixes 502)
- **Event API parsing** — frontend read events as flat array but API returns `{events:[...], total, ...}` object
- **CSV export** — `doc.as<JsonArray>()` → `doc["events"].as<JsonArray>()`

### Changed
- Alarm notifications now show fusion source (radar/csi/both)
- CSI-only alarm uses entry delay (behavior=0) with zone="csi_only"

## [4.2.0-poe-wifi] - 2026-04-13

### Added
- **WiFi CSI: ESPectre port** — Hampel outlier filter (MAD-based, window=7)
- **WiFi CSI: Low-pass filter** — 1st-order Butterworth IIR at 11 Hz cutoff
- **WiFi CSI: CV normalization** — gain-invariant turbulence (std/mean) for ESP32 without AGC lock
- **WiFi CSI: DNS traffic generator** — FreeRTOS task sending UDP queries to gateway at 100 pps
- **WiFi CSI: Breathing-aware presence hold** — prevents dropping stationary person (~5 min max)
- **WiFi CSI: HT20/11n WiFi forcing** — consistent 64 subcarriers with guard-band-aware selection
- **WiFi CSI: STBC packet handling** — collapsed doubled packets (256→128 bytes)
- **WiFi CSI: Short HT20 handling** — 114-byte packets remapped with left guard padding
- **WiFi CSI: CSI packet length validation** — rejects non-standard packets
- **Radar: Entry/exit path validation** — zone `valid_prev_zone` field, invalid path → immediate trigger
- **API: Event timeline** — `current_zone`, `debounce_frames`, `last_event` in `/api/alarm/status`
- **API: Debounce frames** — configurable via `/api/alarm/config` POST

### Changed
- Radar processing tick: 1s → **50ms** (20 Hz) to catch short detections
- MQTT TIER 1 state changes: lastPub cache updated only on successful publish
- CSI subcarriers: `{6,10,...}` → `{12,14,16,18,20,24,28,36,40,44,48,52}` (out of guard bands)
- CSI temporal smoothing: 3/6 enter → **4/6** (matches ESPectre MVS)
- CSI idle amplitude baseline: placeholder → real amplitude sum
- CSI two-pass variance for per-packet turbulence (numerically stable on float32)

## [4.1.4-poe-wifi] - 2026-04-12

### Added
- WiFi CSI runtime configuration via REST API and GUI
- CSI tab in web dashboard with live sparkline graph
- Auto-calibration, idle baseline reset, WiFi reconnect actions
- CSI metrics in SSE telemetry stream

## [4.1.3-poe-wifi] - 2026-04-10

### Changed
- Swapped me-no-dev/AsyncTCP + ESPAsyncWebServer for ESP32Async community fork
- Fixes race conditions in digest auth parser and TCP close handling

## [4.1.2-poe-wifi] - 2026-04-09

### Fixed
- SSE live telemetry buffer regression from LD2412 v3.10.0 port

## [4.1.1-poe-wifi] - 2026-04-08

### Changed
- Security hardening from ESPHome 2026.3 community audit

## [4.1.0-poe-wifi] - 2026-04-05

### Added
- Static IP configuration
- Scheduled arm/disarm with timezone support
- CSV event export
- Auto-arm after configurable idle period
- Heap optimizations

## [4.0.6-poe-wifi] - 2026-03-28

### Added
- Heap diagnostics with crash guards and bounds validation

## [4.0.5-poe-wifi] - 2026-03-27

### Added
- Telegram alerts for low RAM (warn/critical/recover thresholds)

## [4.0.4-poe-wifi] - 2026-03-26

### Added
- Telegram alerts for chip temperature

## [4.0.3-poe-wifi] - 2026-03-25

### Added
- Chip temperature MQTT publishing with configurable interval

## [4.0.2-poe-wifi] - 2026-03-24

### Added
- OTA rollback bootloader
- Chip temperature monitoring

## [4.0.0-poe-wifi] - 2026-03-22

### Added
- LAN8720A PHY LED control via MDIO

## [3.9.9-poe-wifi] - 2026-03-21

### Added
- Web assets on LittleFS with PROGMEM fallback

## [3.9.8-poe-wifi] - 2026-03-20

### Added
- MQTT offline buffer: queue messages to LittleFS when disconnected

## [3.9.5-poe-wifi] - 2026-03-18

### Added
- Initial WiFi CSI implementation (basic turbulence, phase, ratio, breathing)
- WiFi STA mode alongside Ethernet for CSI-only capture
