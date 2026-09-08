#pragma once

#ifndef FW_VERSION
#error "FW_VERSION must be defined by the PlatformIO environment"
#endif

// Stable, machine-readable marker for release tooling. Framework binaries also
// contain semantic-version strings, so a bare vX.Y.Z search is ambiguous.
#define FW_VERSION_BINARY_MARKER "POE2412_FW_VERSION=" FW_VERSION
