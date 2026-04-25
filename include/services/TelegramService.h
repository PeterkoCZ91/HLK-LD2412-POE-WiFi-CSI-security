#ifndef TELEGRAM_SERVICE_H
#define TELEGRAM_SERVICE_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <AsyncTelegram2.h>
#include <Preferences.h>
#include "LD2412Service.h"

class SecurityMonitor;

// Message queue entry for async send
struct TelegramQueueItem {
    char text[512];
    uint8_t retries = 0;
};

class TelegramService {
public:
    TelegramService();

    void begin(Preferences* prefs);
    void update();
    void setRadarService(LD2412Service* radar) { _radar = radar; }
    void setSecurityMonitor(SecurityMonitor* secMon) { _secMon = secMon; }
    void setRebootFlag(volatile bool* flag) { _shouldReboot = flag; }

    // Send messages (non-blocking — enqueues to background task)
    bool sendMessage(const String& text);
    bool sendAlert(const String& title, const String& details = "");

    // Blocking send — returns real result (for diagnostics/test endpoint)
    bool sendMessageDirect(const String& text);

    // Configuration
    void setEnabled(bool enabled);
    void setToken(const char* token);
    void setChatId(const char* chatId);

    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    const char* getToken() const { return _token; }
    const char* getChatId() const { return _chatId; }

private:
    // Background task function
    static void telegramTaskFunc(void* param);
    void telegramLoop();
    void handleNewMessages();
    void processCommand(const String& command, const String& chatId);

    WiFiClientSecure _client;
    AsyncTelegram2* _bot;
    Preferences* _prefs;
    LD2412Service* _radar = nullptr;
    SecurityMonitor* _secMon = nullptr;
    volatile bool* _shouldReboot = nullptr;

    char _token[50];
    char _chatId[20];
    bool _enabled;
    bool _connected;

    unsigned long _lastCheck;
    unsigned long _checkInterval;
    unsigned long _muteStartTime;
    unsigned long _muteDuration;

    // Async send queue
    QueueHandle_t _sendQueue = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    SemaphoreHandle_t _sendMutex = nullptr;
    static constexpr size_t QUEUE_SIZE = 32; // alarm bursts produce 5+ messages, 16 was tight
    uint16_t _droppedMessages = 0;
};

#endif
