#ifndef CSI_MODEL_EXPORT_H
#define CSI_MODEL_EXPORT_H

#include "services/CsiSiteModel.h"

// P1.5 Model export/import (navrh sekce 17.5). Pure helpers shared by the
// export/import endpoints; native-testable. JSON (de)serialization itself lives
// in WebRoutes where ArduinoJson is available — the safety-critical parts
// (validation, checksum) reuse CsiSiteModel.h, already covered by test_csi_model.

// Algorithm-compatibility version: bump when the detection math changes enough
// that a model learned under an older algorithm must not be silently applied.
// Import rejects models whose algo-compat differs from the running firmware.
constexpr uint32_t CSI_MODEL_ALGO_COMPAT = 1;

// Anonymized BSSID fingerprint for export (navrh 17.5 + privacy 17.11). A 32-bit
// CRC of the 6-byte (48-bit) BSSID is lossy — the raw MAC cannot be reconstructed
// from it — yet it is stable across devices, so "is this the same AP?" stays
// answerable without ever exporting the MAC. Returns 0 for an unset BSSID.
inline uint32_t csiBssidHash(const uint8_t bssid[6]) {
    bool anyNonZero = false;
    for (int i = 0; i < 6; i++) if (bssid[i]) { anyNonZero = true; break; }
    if (!anyNonZero) return 0;
    return csiModelCrc32(bssid, 6);
}

#endif // CSI_MODEL_EXPORT_H
