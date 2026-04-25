#ifndef NO_BLUETOOTH
#include "services/BluetoothService.h"
#include "debug.h"
#include <ETH.h>

// Custom UUIDs
const char* BluetoothService::SERVICE_CONFIG_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const char* BluetoothService::CHAR_WIFI_UUID      = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const char* BluetoothService::CHAR_RESTART_UUID   = "d949c585-1d6f-4d9f-a83a-48f86f8a845a";
const char* BluetoothService::SERVICE_STATUS_UUID = "3e766e4a-4363-45f8-8f8d-4e2e288e2a3c";
const char* BluetoothService::CHAR_INFO_UUID      = "c0ffee01-4363-45f8-8f8d-4e2e288e2a3c";

BluetoothService::BluetoothService() {}

void BluetoothService::begin(const char* deviceName, ConfigManager* config) {
    _config = config;
    _startTime = millis();
    _isRunning = true;

    DBG("BT", "Starting NimBLE with name: %s", deviceName);

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Strong signal

    // BLE Security: require passkey pairing for all characteristics
    NimBLEDevice::setSecurityAuth(true, true, true);   // bonding, MITM protection, secure connections
    NimBLEDevice::setSecurityPasskey(142536);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    DBG("BT", "Security enabled, passkey: 142536");

    _server = NimBLEDevice::createServer();

    // 1. Config Service
    _configService = _server->createService(SERVICE_CONFIG_UUID);

    // WiFi characteristic (Write only, encrypted)
    NimBLECharacteristic* pWiFiChar = _configService->createCharacteristic(
        CHAR_WIFI_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC
    );
    _wifiCb.setConfig(_config);
    pWiFiChar->setCallbacks(&_wifiCb);

    // Action characteristic (Restart, encrypted)
    NimBLECharacteristic* pActionChar = _configService->createCharacteristic(
        CHAR_RESTART_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC
    );
    pActionChar->setCallbacks(&_actionCb);

    _configService->start();

    // 2. Status Service (Read only)
    _statusService = _server->createService(SERVICE_STATUS_UUID);
    NimBLECharacteristic* pInfoChar = _statusService->createCharacteristic(
        CHAR_INFO_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC
    );
    
    // Initial info
    String info = "IP: " + ETH.localIP().toString() + " | ETH: " + String(ETH.linkUp() ? "UP" : "DOWN");
    pInfoChar->setValue(info.c_str());
    
    _statusService->start();

    // Advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_CONFIG_UUID);
    pAdvertising->start();

    DBG("BT", "BLE Active and Advertising");
}

void BluetoothService::update() {
    if (!_isRunning) return;

    // Handle timeout
    if (_timeoutSeconds > 0 && (millis() - _startTime) > (_timeoutSeconds * 1000)) {
        DBG("BT", "Timeout reached, stopping BLE to save resources");
        stop();
    }
}

void BluetoothService::stop() {
    if (!_isRunning) return;
    
    NimBLEDevice::deinit(true);
    _isRunning = false;
    DBG("BT", "BLE Stack stopped");
}

// --- Callbacks Implementation ---

void BluetoothService::WiFiCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
        DBG("BT", "WiFi Config received via BLE");
        
        size_t comma = value.find(',');
        if (comma != std::string::npos) {
            String ssid = String(value.substr(0, comma).c_str());
            String pass = String(value.substr(comma + 1).c_str());
            
            DBG("BT", "New SSID: %s", ssid.c_str());
            
            // Note: We need a way to save this to Preferences
            // Since we have pointer to ConfigManager, we can't directly write to Preferences here
            // without exposing Preferences or adding a method to ConfigManager.
            // For now, let's assume we'll add saveWiFi to ConfigManager or use a global.
            // For simplicity in this lab, we'll use Preferences directly if we can't update ConfigManager.
            
            Preferences prefs;
            prefs.begin("radar_config", false);
            prefs.putString("bk_ssid", ssid);
            prefs.putString("bk_pass", pass);
            prefs.end();
            
            DBG("BT", "WiFi credentials saved. Restarting...");
            delay(1000);
            ESP.restart();
        }
    }
}

void BluetoothService::ActionCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    if (value == "restart") {
        DBG("BT", "Restart command received via BLE");
        delay(500);
        ESP.restart();
    }
}
#endif // NO_BLUETOOTH
