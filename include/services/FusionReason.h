#ifndef FUSION_REASON_H
#define FUSION_REASON_H
// #8 Fusion explainability: turn the fusion-source bitmask + confidence into a
// short human "why" line for the dashboard panel. Pure/Arduino-free so it
// host-tests like CsiHealthReasons.h. Bit layout matches SecurityMonitor's
// _fusionSource (radar=1, csi=2, ml=4).
#include <cstdint>
#include <cstdio>

enum FusionSrcBit : uint8_t {
    FUSION_RADAR = 1u << 0,
    FUSION_CSI   = 1u << 1,
    FUSION_ML    = 1u << 2,
};

// Writes a short reason into buf (always NUL-terminated), returns chars written
// (excluding the terminator, clamped to bufLen-1 on truncation).
inline int fusionReasonStr(uint8_t src, float confidence, char* buf, unsigned bufLen) {
    if (buf == nullptr || bufLen == 0) return 0;
    const bool r = (src & FUSION_RADAR) != 0;
    const bool c = (src & FUSION_CSI) != 0;
    const bool m = (src & FUSION_ML) != 0;
    const char* txt;
    if (r && c && m)        txt = "radar + CSI + ML agree";
    else if (r && c)        txt = "radar + CSI agree";
    else if (r && m)        txt = "radar + ML see motion, CSI disagrees";
    else if (c && m)        txt = "CSI + ML (radar blind or absent)";
    else if (r)             txt = "radar only, CSI/ML disagree";
    else if (c)             txt = "CSI only (radar blind or absent)";
    else if (m)             txt = "ML only — weak, untrusted";
    else                    txt = "quiet — no sensor sees motion";
    int n = snprintf(buf, bufLen, "%s (%.0f%%)", txt, confidence * 100.0f);
    if (n < 0) { buf[0] = '\0'; return 0; }
    if ((unsigned)n >= bufLen) n = (int)bufLen - 1;  // snprintf truncated but NUL-terminated
    return n;
}

#endif  // FUSION_REASON_H
