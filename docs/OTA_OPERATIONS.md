# OTA Operations Guide

This guide is for humans and AI agents that deploy firmware to POE-2412 WiFi devices. Treat OTA as a field-service operation: a wrong target or wrong artifact can create physical-access work.

## Core Rules

1. Never flash a device until its identity is verified.
2. Prefer Pull OTA with MD5 when the running firmware supports `/api/update/pull`.
3. Use `espota` only when Pull OTA is unavailable or intentionally being tested.
4. Never use multipart `/api/update` for field devices unless the risk is accepted; it has historically been the least reliable path.
5. Never reuse a firmware version string for different source code.
6. Keep a preflight and postflight record: target identity, old firmware, new firmware, artifact size, MD5, OTA method, result.

## Why OTA Gets Harder the Longer a Device Has Been Running

The single most common field complaint is: *"OTA works right after a flash, but fails on a unit that has been up for days/weeks."* This is expected behaviour of the platform under sustained load, not a random glitch. Three things degrade with uptime:

1. **Heap fragmentation.** The radar UART parser, the WiFi CSI sniffer, and MQTT churn allocate and free buffers constantly. After long uptime the heap can have plenty of *total* free bytes but no single *contiguous* block large enough for a TLS session (`WiFiClientSecure`) or the `Update` write buffer. The allocation fails and the OTA aborts early. Watch `heap_largest` in `/api/ota/status`, not just `heap_free`.

2. **AsyncTCP backpressure under runtime load.** With radar + CSI + MQTT all active, the LwIP/AsyncTCP path on the LAN8720A stalls on large *inbound* uploads — the classic multipart `/api/update` freeze around 64 KB. This is why multipart is fallback-only.

3. **`ArduinoOTA.handle()` starvation.** Under full runtime load the OTA handler may not get the CPU in espota's authentication window, so `espota.py` reports `No Answer to our Authentication` or `Authentication Failed` even with the correct password.

### How each mechanism addresses it

| Mechanism | What it fixes |
|---|---|
| **Cold reboot before OTA** | Defragments the heap and clears AsyncTCP/Update state, so the upload runs on a clean stack. The single biggest reliability win for a long-running unit. |
| **Pull OTA** (`/api/update/pull`) | The device *fetches* the firmware (outbound), bypassing the inbound AsyncTCP backpressure that stalls multipart. Preferred normal path. |
| **espota maintenance window** (`/api/ota/espota/prepare`) | Temporarily backs off CSI/MQTT and suspends the radar task, freeing CPU and RAM so `ArduinoOTA` reliably wins its auth window. Use when you must use espota on a busy unit. |
| **OTA runtime owner + watchdog** | If any OTA path stalls or a TCP upload drops mid-stream, a main-loop watchdog aborts the half-finished update, clears the CSI/MQTT OTA flags, and resumes the radar — so a failed attempt no longer leaves the node degraded until a physical reboot. |
| **Mandatory MD5 + persistent status** | A long pull over a saturated link can corrupt; MD5 fails closed instead of booting a bad image, and the failure reason survives the reboot in `/api/update/pull/status`. |

**Practical rule for a unit with long uptime:** reboot it first, wait until `/api/version` answers again, then Pull OTA. The simplest way is `tools/pull_ota_deploy.sh … --cold-reboot --flash`, which does the reboot-and-wait for you after verifying identity. By hand: `POST /api/restart` (or the GUI "Reboot before OTA" toggle), wait for `/api/version`, then pull. If Pull OTA is unavailable, open the espota maintenance window and run `espota.py` immediately.

## What You Need Before Upload

- Target HTTP host/IP.
- HTTP username and password.
- One or more expected identity values:
  - Ethernet/chip MAC address,
  - MQTT device id,
  - hostname.
- Firmware `.bin` built for the correct environment.
- Firmware MD5 checksum.
- A machine on the same LAN as the ESP, or another HTTP server reachable by the ESP.
- A clear expected current firmware version if you are updating a known device.

## Recommended Tool

Use the guarded Pull OTA helper:

```bash
tools/pull_ota_deploy.sh \
  --host DEVICE_IP \
  --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
  --expect-mac EXPECTED_MAC \
  --expect-id EXPECTED_MQTT_ID \
  --expect-current-fw CURRENT_VERSION
```

This is a dry-run. It verifies target identity and artifact hash, then stops.

To actually flash, add `--flash`:

```bash
tools/pull_ota_deploy.sh \
  --host DEVICE_IP \
  --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
  --expect-mac EXPECTED_MAC \
  --expect-id EXPECTED_MQTT_ID \
  --expect-current-fw CURRENT_VERSION \
  --expect-new-fw NEW_VERSION \
  --flash
```

The script refuses to continue unless at least one identity check is provided. In production, use at least two identity checks.

For a unit that has been running a long time, add `--cold-reboot`. After verifying identity, the helper reboots the target, waits for it to come back, and only then starts the pull — handling the heap-fragmentation / AsyncTCP degradation described below without a manual step:

```bash
tools/pull_ota_deploy.sh \
  --host DEVICE_IP \
  --firmware firmware-vX.Y.Z-poe-wifi-csi.bin \
  --expect-mac EXPECTED_MAC \
  --expect-id EXPECTED_MQTT_ID \
  --expect-current-fw CURRENT_VERSION \
  --expect-new-fw NEW_VERSION \
  --cold-reboot \
  --flash
```

## Manual Preflight

If you are not using the helper, run these checks first:

```bash
curl -sS --connect-timeout 5 http://DEVICE_IP/api/version
curl -sS --connect-timeout 5 --digest -u USER:PASS http://DEVICE_IP/api/health
```

Confirm:

- `fw_version` is the expected currently running firmware.
- `ethernet.ip` matches the device you intend to update.
- `ethernet.mac` or `chip.mac` matches the intended device.
- `mqtt.id` matches the intended device.
- `hostname` is expected.
- `reboot_inhibit` is not blocking normal manual operations unless the OTA path explicitly bypasses it.

Stop if any identity value is unexpected.

## Artifact Check

Build the correct environment:

```bash
pio run -e esp32_poe_csi
```

For radar-only firmware:

```bash
pio run -e esp32_poe
```

Copy the output to a versioned filename:

```bash
cp .pio/build/esp32_poe_csi/firmware.bin firmware-vX.Y.Z-poe-wifi-csi.bin
md5sum firmware-vX.Y.Z-poe-wifi-csi.bin
wc -c firmware-vX.Y.Z-poe-wifi-csi.bin
```

The version in the filename must match the firmware version exposed by `/api/version` after upload.

## Pull OTA Flow

Pull OTA means the ESP downloads the firmware from a URL and flashes itself. This avoids the unreliable large multipart upload path.

> The device does **not** follow HTTP redirects (since v5.0.6). The LAN whitelist only checks the URL you provide, so a `30x` to an off-LAN host would defeat it and could leak the forwarded `Authorization` header. Point the URL directly at the firmware `.bin`; a redirect response fails the pull with a clear message. Note also that HTTPS uses `setInsecure()` (no certificate validation) — the enforced MD5 is the firmware integrity anchor, so always supply it.

1. Serve the firmware from a machine reachable by the ESP:

```bash
python3 -m http.server 8001 --bind 0.0.0.0
```

2. Confirm the ESP can fetch the file URL from the LAN:

```bash
curl -I http://LAN_HOST_IP:8001/firmware-vX.Y.Z-poe-wifi-csi.bin
```

3. Start Pull OTA with MD5:

```bash
curl -sS --digest -u USER:PASS \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://LAN_HOST_IP:8001/firmware-vX.Y.Z-poe-wifi-csi.bin","md5":"32_HEX_MD5"}' \
  http://DEVICE_IP/api/update/pull
```

4. Poll status:

```bash
curl -sS --digest -u USER:PASS http://DEVICE_IP/api/update/pull/status
```

Expected phases include:

- `accepted`
- `connecting`
- `fetching`
- `success_rebooting`
- `failed`

5. After reboot, verify:

```bash
curl -sS --connect-timeout 5 http://DEVICE_IP/api/version
curl -sS --digest -u USER:PASS http://DEVICE_IP/api/health
curl -sS --digest -u USER:PASS http://DEVICE_IP/api/ota/status
```

## espota Maintenance Window

Newer firmware exposes an authenticated maintenance endpoint:

```bash
curl -sS --digest -u USER:PASS -X POST \
  'http://DEVICE_IP/api/ota/espota/prepare?seconds=120'
```

This does not flash firmware. It opens a short runtime window where services back off and `ArduinoOTA` has a better chance to receive an upload.

Then run `espota.py` from the same LAN:

```bash
python3 /path/to/espota.py -i DEVICE_IP -p 3232 -a OTA_PASSWORD \
  -f firmware-vX.Y.Z-poe-wifi-csi.bin
```

Use this mainly for recovery or for testing espota itself. Pull OTA with MD5 is the preferred normal path when available.

## Rollback Limits

The ESP has two OTA slots, but rollback is not a complete recovery strategy. It helps when the latest image cannot boot or fails validation. It does not help much if several consecutive releases all contain the same OTA bug; both slots can eventually contain bad builds.

Therefore the project relies on multiple layers:

- mandatory artifact hash for Pull OTA;
- target identity checks before upload;
- OTA runtime owner/timeout cleanup;
- persistent OTA status and reset history;
- espota maintenance window as an alternate path;
- release checks that prevent shipping another OTA-regression build.

## What AI Agents Must Do

Before flashing:

1. Read this guide.
2. Verify the target identity using `/api/health`.
3. Verify the current firmware version.
4. Verify the firmware artifact filename, size, and MD5.
5. Prefer `tools/pull_ota_deploy.sh` dry-run first.
6. Only use `--flash` when the identity and artifact are unambiguous.

After flashing:

1. Verify `/api/version` equals the intended new version.
2. Verify `/api/health` still responds.
3. Verify `/api/ota/status` if supported by the firmware.
4. Record old version, new version, target identity, artifact MD5, and result.

Stop immediately if:

- more than one ESP could match the target;
- MAC, MQTT id, hostname, or expected firmware version does not match;
- the artifact filename/version does not match the intended release;
- MD5 is missing;
- Pull OTA reports `failed`;
- the device does not return after reboot.

## Do Not Do This

- Do not flash by IP alone.
- Do not flash a root `firmware.bin` without a versioned filename.
- Do not assume DHCP addresses are stable.
- Do not use stale Claude/Codex memory as identity proof; always query the device.
- Do not retry failing OTA loops blindly.
- Do not upload to production using a release candidate unless the operator explicitly accepts that risk.
