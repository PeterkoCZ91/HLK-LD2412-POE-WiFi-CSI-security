#ifndef ETH_DEFAULT_NETIF_POLICY_H
#define ETH_DEFAULT_NETIF_POLICY_H

#include <stdint.h>

// Decides when to force Ethernet back as lwIP's default netif on this
// dual-homed node (ETH + a CSI-capture WiFi STA, both on one flat subnet).
//
// Why this exists: WiFi STA outranks ETH on esp_netif route_prio, so lwIP hands
// the default to WiFi every time that interface gets an IP. csi7 already had a
// one-shot fix for it; this policy is what makes the correction hold.
enum class NetifAction {
    None,     // Ethernet already holds the default — stay off the core lock
    Assert,   // take the default back
    SkipOta,       // an OTA transfer is in flight — lwIP is off limits (csi7b)
    SkipLinkDown,  // Ethernet has no carrier or no IP — nothing to point at
};

class EthDefaultNetifPolicy {
public:
    explicit EthDefaultNetifPolicy(uint32_t retryIntervalMs = 5000)
        : _retryIntervalMs(retryIntervalMs) {}

    NetifAction evaluate(uint32_t nowMs, bool otaInProgress,
                         bool ethLinkUp, bool ethHasIp, bool ethIsDefault) {
        if (ethIsDefault) {
            _notDefaultTracked = false;
            return NetifAction::None;
        }
        // Why the default was lost decides whether a live socket has to move.
        // ETH carrier dropped: lwIP handed the default to WiFi and will hand it
        // straight back — a flap, and any ETH-borne session broke on its own.
        // ETH stayed up throughout: WiFi took the default on its own (DHCP
        // renewal, higher route_prio) and the live session really is stranded
        // on the wrong interface. Only the second case earns a reconnect.
        if (!_notDefaultTracked) {
            _notDefaultTracked = true;
            _lossHadLinkDown   = false;
        }
        if (!ethLinkUp) _lossHadLinkDown = true;
        if (otaInProgress) return NetifAction::SkipOta;
        if (!ethLinkUp || !ethHasIp) return NetifAction::SkipLinkDown;
        // Unsigned subtraction, so a millis() wrap costs at most one late retry
        // rather than parking the policy for 49 days.
        if (_hasAsserted && (uint32_t)(nowMs - _lastAssertMs) < _retryIntervalMs) {
            return NetifAction::None;
        }
        _lastAssertMs = nowMs;
        _hasAsserted  = true;
        _lastLossWasFlap = _lossHadLinkDown;
        _assertCount++;
        return NetifAction::Assert;
    }

    // Reported by the caller after it has run netif_set_default(): did the
    // default actually move? Only a real move invalidates live sockets.
    void noteAsserted(bool defaultChanged) {
        if (!defaultChanged) return;
        _changeCount++;
        if (!_lastLossWasFlap) _routingChanged = true;
    }

    // One-shot: consuming it hands the caller responsibility for the reconnect.
    bool takeRoutingChanged() {
        bool v = _routingChanged;
        _routingChanged = false;
        return v;
    }

    uint32_t assertCount() const { return _assertCount; }
    uint32_t changeCount() const { return _changeCount; }

private:
    uint32_t _retryIntervalMs;
    bool     _notDefaultTracked = false;
    bool     _lossHadLinkDown   = false;
    bool     _lastLossWasFlap   = false;
    uint32_t _lastAssertMs = 0;
    bool     _hasAsserted  = false;
    uint32_t _assertCount  = 0;
    uint32_t _changeCount  = 0;
    bool     _routingChanged = false;
};

#endif  // ETH_DEFAULT_NETIF_POLICY_H
