#ifndef BLUETOOTH_SERVICE_H
#define BLUETOOTH_SERVICE_H

#include <Arduino.h>

#ifdef NO_BLUETOOTH

// Stub class — compiles to nothing, zero RAM
class BluetoothService {
public:
    void begin(const char*, void*) {}
    void update() {}
    void stop() {}
    void setTimeout(uint32_t) {}
    bool isRunning() const { return false; }
};

#else

#include <NimBLEDevice.h>
#include "ConfigManager.h"

/**
 * @brief Bluetooth (BLE) Service for remote configuration and diagnostics.
 *
 * Uses NimBLE for low memory footprint. Allows configuring WiFi, viewing status,
 * and performing system actions without network connectivity.
 */
class BluetoothService {
public:
    BluetoothService();

    /**
     * @brief Initialize BLE service
     * @param deviceName Name for BLE advertising
     * @param config Reference to configuration manager for saving settings
     */
    void begin(const char* deviceName, ConfigManager* config);

    /**
     * @brief Update task - handles timeouts and state
     */
    void update();

    /**
     * @brief Stop BLE advertising and release resources
     */
    void stop();

    /**
     * @brief Set how long BT stays active after boot (0 = forever)
     */
    void setTimeout(uint32_t seconds) { _timeoutSeconds = seconds; }

    bool isRunning() const { return _isRunning; }

private:
    ConfigManager* _config = nullptr;
    bool _isRunning = false;
    uint32_t _timeoutSeconds = 600; // Default 10 minutes
    unsigned long _startTime = 0;

    NimBLEServer* _server = nullptr;
    NimBLEService* _configService = nullptr;
    NimBLEService* _statusService = nullptr;

    // UUIDs for Service and Characteristics
    static const char* SERVICE_CONFIG_UUID;
    static const char* CHAR_WIFI_UUID;
    static const char* CHAR_RESTART_UUID;

    static const char* SERVICE_STATUS_UUID;
    static const char* CHAR_INFO_UUID;

    // Callbacks (owned by BluetoothService, not heap-allocated)
    class WiFiCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
        ConfigManager* _cfg = nullptr;
    public:
        WiFiCallbacks() {}
        void setConfig(ConfigManager* cfg) { _cfg = cfg; }
    };

    class ActionCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    };

    WiFiCallbacks _wifiCb;
    ActionCallbacks _actionCb;
};

#endif // NO_BLUETOOTH
#endif // BLUETOOTH_SERVICE_H
