# Release Checklist

Use this checklist to keep the repository up to date with minimal overhead.

## Before Build

1. Bump `FW_VERSION` in `src/main.cpp` and any build flag override in `platformio.ini`. Never reuse a version string for different source code.
2. Read [OTA Operations Guide](OTA_OPERATIONS.md) if the release may be deployed over the network.
3. Confirm `include/secrets.h` and `include/known_devices.h` are still untracked.
4. Decide whether the change is:
   - internal-only
   - user-visible
   - release-worthy

## After Code Changes

1. Add or update the release entry in `CHANGELOG.md`.
2. If the change is user-visible, update the relevant `README.md` sections:
   - latest release callout
   - features
   - API endpoints
   - known issues
   - roadmap
3. If the web UI changed, replace the affected files in `docs/screenshots/` with anonymized captures.

## Build And Verify

1. Run native unit tests and static analysis:

```bash
pio test -e native
bash tools/run_cppcheck.sh
```

2. Build the CSI firmware:

```bash
pio run -e esp32_poe_csi
```

3. If needed, also build the radar-only variant:

```bash
pio run -e esp32_poe
```

4. Verify the version string in the firmware output and serial boot log.

## OTA Deployment Gate

Before flashing any device over the network:

1. Run a dry-run identity check with `tools/pull_ota_deploy.sh`.
2. Verify at least one hardware identity value, preferably MAC plus MQTT id or hostname.
3. Record current firmware, target identity, artifact size, and MD5.
4. Prefer Pull OTA with MD5. Use multipart `/api/update` only as an accepted-risk fallback.
5. After flashing, verify `/api/version`, `/api/health`, and `/api/ota/status` when supported.

## Local Artifact Handling

If you keep local firmware binaries in the project root for deployment, use a versioned filename:

```bash
firmware-vX.Y.Z-poe-wifi-csi.bin
```

Those binaries are local release artifacts, not part of the public documentation set.

## Before Push

1. Run `git diff --stat` and scan the changed files.
2. Make sure no private infrastructure details leaked into code, docs, screenshots, or commit messages.
3. Commit code and doc updates together.

## If The Device Is Not Reachable

Still publish the source and documentation updates if the release is real. Deployment confirmation can wait. Do not leave `README.md` and `CHANGELOG.md` stale just because remote OTA or domain access is unavailable.
