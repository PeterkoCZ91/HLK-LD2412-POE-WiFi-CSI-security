#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ETH.h>
#include <Preferences.h>
#include <atomic>

// NotificationType and AlertSource live with the cooldown policy, which is the
// code that has to reason about both of them together.
#include "services/NotificationCooldownPolicy.h"

// Forward declaration
class TelegramService;

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
    // `source` identifies the producer for cooldown purposes. Leaving it
    // GENERIC keeps the old per-type rate limiting; naming it stops one
    // producer from muting a different one that shares its type.
    bool sendAlert(NotificationType type, const String& message, const String& details = "",
                   AlertSource source = AlertSource::GENERIC);

    // Configuration
    void setTelegramConfig(const char* token, const char* chatId);
    void setDiscordWebhook(const char* webhook);
    void setGenericWebhook(const char* webhook);
    void setEnabled(bool enabled);
    void setCooldown(unsigned long ms);

    // Getters
    bool isEnabled() const { return _config.enabled; }
    bool isWebhookTlsBlocked() const { return _tlsBlockedMask.load(std::memory_order_relaxed) != 0; }
    const NotificationConfig& getConfig() const { return _config; }

    bool sendTelegram(const String& message);

private:
    bool enqueueDiscord(const String& message, const String& title);
    bool enqueueGenericWebhook(const String& payload);

    static void webhookTaskFunc(void* param);
    void processWebhookQueue();

    String formatMessage(NotificationType type, const String& message, const String& details);
    bool checkCooldown(NotificationType type, AlertSource source);
    const char* getTypeString(NotificationType type);

    NotificationConfig _config;
    char _deviceName[32] = "Unknown";
    Preferences* _prefs;
    TelegramService* _telegramService = nullptr;

    // Async webhook queue
    QueueHandle_t _webhookQueue = nullptr;
    TaskHandle_t _webhookTask = nullptr;
    static constexpr size_t WEBHOOK_QUEUE_SIZE = 4;
    // One bit per configured webhook type; exposed through /healthz so a
    // fail-closed TLS policy never turns alarm delivery into a silent failure.
    std::atomic<uint8_t> _tlsBlockedMask{0};

    // Cooldown tracking, keyed by producer rather than by type
    NotificationCooldownPolicy _cooldown;
};

#endif
