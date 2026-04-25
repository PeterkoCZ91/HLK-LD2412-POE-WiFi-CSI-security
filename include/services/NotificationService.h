#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ETH.h>
#include <Preferences.h>

// Forward declaration
class TelegramService;

// Notification Types
enum class NotificationType {
    TAMPER_ALERT,
    PRESENCE_DETECTED,
    PRESENCE_CLEARED,
    SYSTEM_ERROR,
    WIFI_ANOMALY,
    HEALTH_WARNING,
    ALARM_STATE_CHANGE,
    ENTRY_DETECTED,
    ALARM_TRIGGERED       // FIX #2: dedicated type for real alarm triggers (never tamper-gated)
};

struct NotificationConfig {
    bool enabled = false;
    char telegram_token[64];
    char telegram_chat_id[32];
    char discord_webhook[256];
    char generic_webhook[256];
    bool notify_on_tamper = true;
    bool notify_on_presence = false;  // Usually too noisy
    bool notify_on_errors = true;
    bool notify_on_wifi_anomaly = true;
    unsigned long cooldown_ms = 300000;  // 5 minutes cooldown between notifications
};

// Webhook request for async queue
enum class WebhookType : uint8_t { DISCORD, GENERIC };

struct WebhookRequest {
    WebhookType type;
    char url[256];
    char payload[512];   // FIX #12: increased from 256 to handle approach logs
    char title[48];
    uint8_t retries = 0;
};

class NotificationService {
public:
    NotificationService();

    void begin(Preferences* prefs, const char* deviceName);
    void setTelegramService(TelegramService* tg) { _telegramService = tg; }
    void update();

    // Send notifications
    bool sendAlert(NotificationType type, const String& message, const String& details = "");

    // Configuration
    void setTelegramConfig(const char* token, const char* chatId);
    void setDiscordWebhook(const char* webhook);
    void setGenericWebhook(const char* webhook);
    void setEnabled(bool enabled);
    void setCooldown(unsigned long ms);

    // Getters
    bool isEnabled() const { return _config.enabled; }
    const NotificationConfig& getConfig() const { return _config; }

    bool sendTelegram(const String& message);

private:
    bool enqueueDiscord(const String& message, const String& title);
    bool enqueueGenericWebhook(const String& payload);

    static void webhookTaskFunc(void* param);
    void processWebhookQueue();

    String formatMessage(NotificationType type, const String& message, const String& details);
    bool checkCooldown(NotificationType type);
    const char* getTypeString(NotificationType type);

    NotificationConfig _config;
    char _deviceName[32] = "Unknown";
    Preferences* _prefs;
    TelegramService* _telegramService = nullptr;

    // Async webhook queue
    QueueHandle_t _webhookQueue = nullptr;
    TaskHandle_t _webhookTask = nullptr;
    static constexpr size_t WEBHOOK_QUEUE_SIZE = 4;

    // Cooldown tracking per notification type
    unsigned long _lastNotification[9] = {0};  // One per NotificationType enum
};

#endif
