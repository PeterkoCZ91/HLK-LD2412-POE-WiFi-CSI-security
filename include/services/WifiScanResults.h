#ifndef WIFI_SCAN_RESULTS_H
#define WIFI_SCAN_RESULTS_H

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class WifiScanState : uint8_t {
    IDLE = 0,
    RUNNING,
    READY,
    FAILED,
};

enum class WifiScanStartResult : uint8_t {
    STARTED = 0,
    BUSY,
    FAILED,
};

enum class WifiScanFailureReason : uint8_t {
    NONE = 0,
    CSI_PAUSE_FAILED,
    DRIVER_START_FAILED,
    DRIVER_FAILED,
    TIMEOUT,
    CSI_RESTORE_FAILED,
};

inline const char* wifiScanStateText(WifiScanState state) {
    switch (state) {
        case WifiScanState::IDLE:    return "idle";
        case WifiScanState::RUNNING: return "running";
        case WifiScanState::READY:   return "complete";
        case WifiScanState::FAILED:  return "failed";
    }
    return "failed";
}

inline const char* wifiScanFailureText(WifiScanFailureReason reason) {
    switch (reason) {
        case WifiScanFailureReason::NONE:                return "";
        case WifiScanFailureReason::CSI_PAUSE_FAILED:    return "csi_pause_failed";
        case WifiScanFailureReason::DRIVER_START_FAILED: return "driver_start_failed";
        case WifiScanFailureReason::DRIVER_FAILED:       return "driver_failed";
        case WifiScanFailureReason::TIMEOUT:             return "timeout";
        case WifiScanFailureReason::CSI_RESTORE_FAILED:  return "csi_restore_failed";
    }
    return "unknown";
}

struct WifiScanNetwork {
    char ssid[33] = {};
    int16_t rssi = -127;
    uint8_t channel = 0;
    uint8_t authMode = 0;
    bool current = false;
};

// Fixed-size, Arduino-free result set for native tests and bounded device RAM.
// One row per SSID is kept (the strongest BSSID), sorted by RSSI descending.
class WifiScanResults {
public:
    static constexpr uint8_t CAPACITY = 20;

    void clear() { _count = 0; }
    uint8_t count() const { return _count; }
    const WifiScanNetwork& at(uint8_t index) const { return _items[index]; }

    bool add(const char* ssid, int32_t rssi, int32_t channel,
             uint8_t authMode, bool current) {
        if (ssid == nullptr || ssid[0] == '\0') return false;

        WifiScanNetwork item;
        std::strncpy(item.ssid, ssid, sizeof(item.ssid) - 1);
        item.ssid[sizeof(item.ssid) - 1] = '\0';
        if (rssi < -127) rssi = -127;
        if (rssi > 0) rssi = 0;
        if (channel < 0) channel = 0;
        if (channel > 255) channel = 255;
        item.rssi = static_cast<int16_t>(rssi);
        item.channel = static_cast<uint8_t>(channel);
        item.authMode = authMode;
        item.current = current;

        for (uint8_t i = 0; i < _count; i++) {
            if (std::strcmp(_items[i].ssid, item.ssid) != 0) continue;
            if (item.rssi <= _items[i].rssi) {
                _items[i].current = _items[i].current || item.current;
                return false;
            }
            item.current = item.current || _items[i].current;
            _items[i] = item;
            sortUp(i);
            return true;
        }

        uint8_t index;
        if (_count < CAPACITY) {
            index = _count++;
        } else {
            if (item.rssi <= _items[CAPACITY - 1].rssi) return false;
            index = CAPACITY - 1;
        }
        _items[index] = item;
        sortUp(index);
        return true;
    }

    uint8_t copyTo(WifiScanNetwork* out, uint8_t capacity) const {
        if (out == nullptr || capacity == 0) return 0;
        uint8_t n = (_count < capacity) ? _count : capacity;
        for (uint8_t i = 0; i < n; i++) out[i] = _items[i];
        return n;
    }

private:
    void sortUp(size_t index) {
        // Keep the fixed-size buffer safe even if a future caller passes a
        // stale or corrupted index. add() currently guarantees both bounds.
        if (index >= CAPACITY || index >= static_cast<size_t>(_count)) return;
        while (index != 0) {
            const size_t previous = index - 1;
            if (_items[index].rssi <= _items[previous].rssi) break;
            WifiScanNetwork tmp = _items[previous];
            _items[previous] = _items[index];
            _items[index] = tmp;
            index = previous;
        }
    }

    WifiScanNetwork _items[CAPACITY];
    uint8_t _count = 0;
};

#endif // WIFI_SCAN_RESULTS_H
