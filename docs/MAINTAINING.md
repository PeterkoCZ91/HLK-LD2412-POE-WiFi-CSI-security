# Maintaining Public Project Docs

This repository is the public source of truth for the project. Device-specific deployment notes, private network details, and local working memory should stay outside tracked documentation.

## Public vs Private Sources

Use the tracked repository for information that should be visible to anyone:

- `README.md` for project overview, hardware, API, screenshots, and major user-facing features
- `CHANGELOG.md` for released firmware changes
- `docs/screenshots/` for anonymized UI captures

Keep private or local-only information out of tracked files:

- IP addresses, domain names, WiFi SSIDs, MQTT servers, usernames, passwords
- Device hostnames tied to a real installation
- Deployment timestamps, reboot history, and site-specific operational notes
- Scratch notes from experiments or upstream research

If you want local notes for ongoing work, keep them in a gitignored directory.

## Minimal Update Workflow

After each meaningful firmware change:

1. Update `FW_VERSION` in `src/main.cpp` before building a release candidate.
2. Add a concise entry to `CHANGELOG.md` if the change affects firmware behavior, API, UI, security, or deployment.
3. Update `README.md` only when the public-facing story changed:
   - latest documented release callout
   - feature lists
   - API reference
   - known issues
   - screenshots
4. Refresh screenshots in `docs/screenshots/` if the UI changed in a visible way.
5. Build and verify locally.
6. Push code and docs together so the repo stays self-consistent.

## What Not To Block On

Public docs should not depend on live access to the deployed ESP32. If the device is offline or not reachable through a domain:

- document the firmware change from source and build context
- add release notes from the code diff you actually made
- keep deployment status in private notes instead of public docs

That keeps the repository current even when the live device is unavailable.

## Security Scrub Before Push

Before publishing, check that no tracked file contains:

- private IP ranges such as `192.168.x.x`, `10.x.x.x`, or `172.16.x.x`
- real SSIDs or domain names
- MQTT broker addresses
- credentials or auth tokens
- screenshots with hostnames, IPs, or device names visible

The public repo should describe the firmware, not the installation where it runs.
