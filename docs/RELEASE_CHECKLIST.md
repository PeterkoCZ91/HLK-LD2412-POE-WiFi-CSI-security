# Release Checklist

Use this checklist to keep the repository up to date with minimal overhead.

## Before Build

1. Bump `FW_VERSION` in `src/main.cpp`.
2. Confirm `include/secrets.h` and `include/known_devices.h` are still untracked.
3. Decide whether the change is:
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

1. Build the CSI firmware:

```bash
pio run -e esp32_poe_csi
```

2. If needed, also build the radar-only variant:

```bash
pio run -e esp32_poe
```

3. Verify the version string in the firmware output and serial boot log.

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
