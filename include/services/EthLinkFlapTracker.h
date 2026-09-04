#ifndef ETH_LINK_FLAP_TRACKER_H
#define ETH_LINK_FLAP_TRACKER_H

#include <stdint.h>

// Ethernet link flap accounting (field investigation 2026-08-22).
//
// The node was losing its Ethernet link constantly, but every layer that
// reported it aliased the signal into something misleading:
//
//   - the IDF driver polls the PHY every 2000 ms (check_link_period_ms), so a
//     glitch of any length is reported as a whole multiple of 2 s;
//   - the connectivity watchdog samples ETH.linkUp() once a minute, so it
//     logged "ETH link restored after 60s" for a 2 s outage;
//   - MQTT diagnostics publish every 30 s, so Home Assistant's history showed
//     339 episodes/day with a 30 s median.
//
// The driver's own ARDUINO_EVENT_ETH_{CONNECTED,DISCONNECTED} events were
// already firing at the true rate — ~6800/day, 23.5 % of wall-clock down —
// and nothing counted them. This pure tracker consumes those edges and turns
// them into numbers you can act on: is the link still flapping after the
// cable was replaced, and by how much.
//
// Arduino-free so it can be host-tested. All arithmetic is unsigned 32-bit
// millisecond delta, so millis() rollover costs nothing.
//
// Cross-task note: the edges arrive on the Arduino event task while readers
// (health JSON, /healthz, Prometheus) run elsewhere. Every field is a 32-bit
// aligned scalar — torn reads are impossible on ESP32 and a reader may at
// worst lag one edge, which is harmless for diagnostics.

struct EthLinkFlapStats {
    uint32_t downCount     = 0;   // link-down edges since begin()/reset()
    uint32_t downTotalMs   = 0;   // accumulated outage, including one in flight
    uint32_t longestDownMs = 0;   // worst single episode
    uint32_t currentDownMs = 0;   // length of the episode in flight, else 0
    uint32_t sinceMs       = 0;   // observation window this is measured over
    uint16_t downPermille  = 0;   // downTotalMs / sinceMs, in ‰
    bool     linkDown      = false;
};

class EthLinkFlapTracker {
public:
    void begin(uint32_t nowMs) {
        _startMs = nowMs;
        _downCount = 0;
        _downTotalMs = 0;
        _longestDownMs = 0;
        _downSinceMs = 0;
        _linkDown = false;
    }

    // Zero the counters without asserting anything about the current link
    // state — an operator clearing stats mid-outage is still mid-outage.
    void reset(uint32_t nowMs) {
        bool wasDown = _linkDown;
        begin(nowMs);
        if (wasDown) {
            _linkDown = true;
            _downSinceMs = nowMs;
        }
    }

    // ARDUINO_EVENT_ETH_DISCONNECTED. Re-posted by the driver on every poll
    // while the link stays down, so only the falling edge opens an episode.
    void onLinkDown(uint32_t nowMs) {
        if (_linkDown) return;
        _linkDown = true;
        _downSinceMs = nowMs;
        _downCount++;
    }

    // ARDUINO_EVENT_ETH_CONNECTED. Ignored unless an episode is open, so the
    // connect event at boot does not invent a zero-length outage.
    void onLinkUp(uint32_t nowMs) {
        if (!_linkDown) return;
        uint32_t episode = nowMs - _downSinceMs;
        _linkDown = false;
        _downTotalMs += episode;
        if (episode > _longestDownMs) _longestDownMs = episode;
    }

    EthLinkFlapStats stats(uint32_t nowMs) const {
        EthLinkFlapStats s;
        s.downCount     = _downCount;
        s.linkDown      = _linkDown;
        s.sinceMs       = nowMs - _startMs;
        s.currentDownMs = _linkDown ? (nowMs - _downSinceMs) : 0;
        s.downTotalMs   = _downTotalMs + s.currentDownMs;
        s.longestDownMs = s.currentDownMs > _longestDownMs ? s.currentDownMs
                                                           : _longestDownMs;
        if (s.sinceMs) {
            uint64_t permille = ((uint64_t)s.downTotalMs * 1000u) / s.sinceMs;
            s.downPermille = permille > 1000u ? 1000u : (uint16_t)permille;
        }
        return s;
    }

private:
    uint32_t _startMs       = 0;
    uint32_t _downCount     = 0;
    uint32_t _downTotalMs   = 0;
    uint32_t _longestDownMs = 0;
    uint32_t _downSinceMs   = 0;
    bool     _linkDown      = false;
};

#endif // ETH_LINK_FLAP_TRACKER_H
