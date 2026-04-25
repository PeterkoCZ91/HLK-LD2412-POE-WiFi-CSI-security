#ifndef LITE_BUILD
#include "services/NotificationService.h"
#include "services/TelegramService.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <ETH.h>

NotificationService::NotificationService() {}

void NotificationService::begin(Preferences* prefs, const char* deviceName) {
    _prefs = prefs;
    if (deviceName) {
        strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
        _deviceName[sizeof(_deviceName) - 1] = '\0';
    }

    // Load configuration from preferences
    _config.enabled = _prefs->getBool("notif_enabled", false);
    _prefs->getString("tg_token", "").toCharArray(_config.telegram_token, 64);
    _prefs->getString("tg_chat", "").toCharArray(_config.telegram_chat_id, 32);
    _prefs->getString("dc_webhook", "").toCharArray(_config.discord_webhook, 256);
    _prefs->getString("gen_webhook", "").toCharArray(_config.generic_webhook, 256);

    _config.notify_on_tamper = _prefs->getBool("notif_tamper", true);
    _config.notify_on_presence = _prefs->getBool("notif_presence", false);
    _config.notify_on_errors = _prefs->getBool("notif_errors", true);
    _config.notify_on_wifi_anomaly = _prefs->getBool("notif_wifi", true);
    _config.cooldown_ms = _prefs->getULong("notif_cooldown", 300000);

    // Create webhook queue and background task
    bool hasWebhook = (strlen(_config.discord_webhook) > 0 || strlen(_config.generic_webhook) > 0);
    if (_config.enabled && hasWebhook) {
        _webhookQueue = xQueueCreate(WEBHOOK_QUEUE_SIZE, sizeof(WebhookRequest));
        if (_webhookQueue) {
            xTaskCreatePinnedToCore(webhookTaskFunc, "webhook_task", 8192, this, 1, &_webhookTask, 0);
            DBG("Notif", "Webhook background task started");
        }
    }

    if (_config.enabled) {
        DBG("Notif", "Service enabled");
        if (strlen(_config.telegram_token) > 0) DBG("Notif", "Telegram configured");
        if (strlen(_config.discord_webhook) > 0) DBG("Notif", "Discord configured (async)");
        if (strlen(_config.generic_webhook) > 0) DBG("Notif", "Generic webhook configured (async)");
    } else {
        DBG("Notif", "Service disabled");
    }
}

void NotificationService::update() {
    // Background task handles webhook processing
}

// --- Background webhook task ---

void NotificationService::webhookTaskFunc(void* param) {
    NotificationService* self = (NotificationService*)param;
    self->processWebhookQueue();
}

void NotificationService::processWebhookQueue() {
    static constexpr uint8_t MAX_RETRIES = 3;
    WebhookRequest req;
    HTTPClient http;

    for (;;) {
        if (xQueueReceive(_webhookQueue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) continue;

        if (!ETH.linkUp()) {
            // Re-queue if not at retry limit — don't silently drop
            if (req.retries < MAX_RETRIES) {
                req.retries++;
                xQueueSendToFront(_webhookQueue, &req, pdMS_TO_TICKS(10));
            } else {
                DBG("Notif", "Webhook dropped after %d retries (ETH down)", req.retries);
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        http.setTimeout(3000);
        bool sent = false;

        if (req.type == WebhookType::DISCORD) {
            http.begin(req.url);
            http.addHeader("Content-Type", "application/json");

            JsonDocument doc;
            JsonArray embeds = doc["embeds"].to<JsonArray>();
            JsonObject embed = embeds.add<JsonObject>();
            embed["title"] = req.title;
            embed["description"] = req.payload;
            embed["color"] = 15158332;
            JsonObject footer = embed["footer"].to<JsonObject>();
            footer["text"] = "LD2412 Security Node";

            String payload;
            serializeJson(doc, payload);

            int httpCode = http.POST(payload);
            sent = (httpCode == 200 || httpCode == 204);
            if (!sent) DBG("Notif", "Discord failed: HTTP %d (retry %d)", httpCode, req.retries);
            http.end();

        } else if (req.type == WebhookType::GENERIC) {
            http.begin(req.url);
            http.addHeader("Content-Type", "application/json");

            int httpCode = http.POST(req.payload);
            sent = (httpCode >= 200 && httpCode < 300);
            if (!sent) DBG("Notif", "Webhook failed: HTTP %d (retry %d)", httpCode, req.retries);
            http.end();
        }

        if (sent) {
            DBG("Notif", "Webhook sent OK");
        } else if (req.retries < MAX_RETRIES) {
            req.retries++;
            xQueueSendToFront(_webhookQueue, &req, pdMS_TO_TICKS(10));
            vTaskDelay(pdMS_TO_TICKS(2000)); // Backoff before retry
            continue;
        } else {
            DBG("Notif", "Webhook permanently dropped after %d retries", req.retries);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --- Alert dispatch ---

bool NotificationService::sendAlert(NotificationType type, const String& message, const String& details) {
    if (!_config.enabled) return false;

    // Check notification filters
    bool shouldSend = false;
    switch(type) {
        case NotificationType::TAMPER_ALERT:
            shouldSend = _config.notify_on_tamper;
            break;
        case NotificationType::PRESENCE_DETECTED:
        case NotificationType::PRESENCE_CLEARED:
            shouldSend = _config.notify_on_presence;
            break;
        case NotificationType::SYSTEM_ERROR:
        case NotificationType::HEALTH_WARNING:
            shouldSend = _config.notify_on_errors;
            break;
        case NotificationType::WIFI_ANOMALY:
            shouldSend = _config.notify_on_wifi_anomaly;
            break;
        case NotificationType::ALARM_STATE_CHANGE:
        case NotificationType::ENTRY_DETECTED:
        case NotificationType::ALARM_TRIGGERED:
            shouldSend = true;  // Always send security-critical events
            break;
    }

    if (!shouldSend) return false;

    // FIX #2 + #11: Security-critical types bypass cooldown entirely
    bool isCritical = (type == NotificationType::ALARM_TRIGGERED ||
                       type == NotificationType::ALARM_STATE_CHANGE ||
                       type == NotificationType::ENTRY_DETECTED);
    if (!isCritical && !checkCooldown(type)) {
        DBG("Notif", "Cooldown active for %s", getTypeString(type));
        return false;
    }

    // Format message
    String formattedMsg = formatMessage(type, message, details);
    String title = String(getTypeString(type));

    bool success = false;

    // Try Telegram first (already non-blocking via TelegramService)
    if (strlen(_config.telegram_token) > 0 && strlen(_config.telegram_chat_id) > 0) {
        success |= sendTelegram(formattedMsg);
    }

    // Enqueue Discord (non-blocking)
    if (strlen(_config.discord_webhook) > 0) {
        success |= enqueueDiscord(formattedMsg, title);
    }

    // Enqueue Generic Webhook (non-blocking)
    if (strlen(_config.generic_webhook) > 0) {
        JsonDocument doc;
        doc["type"] = getTypeString(type);
        doc["message"] = message;
        if (details.length() > 0) doc["details"] = details;
        doc["device_id"] = ETH.macAddress();
        doc["timestamp"] = millis() / 1000;

        String payload;
        serializeJson(doc, payload);
        success |= enqueueGenericWebhook(payload);
    }

    if (success) {
        _lastNotification[(int)type] = millis();
    }

    return success;
}

bool NotificationService::enqueueDiscord(const String& message, const String& title) {
    if (!_webhookQueue) return false;

    WebhookRequest req;
    req.type = WebhookType::DISCORD;
    strncpy(req.url, _config.discord_webhook, sizeof(req.url) - 1);
    req.url[sizeof(req.url) - 1] = '\0';
    strncpy(req.payload, message.c_str(), sizeof(req.payload) - 1);
    req.payload[sizeof(req.payload) - 1] = '\0';
    strncpy(req.title, title.c_str(), sizeof(req.title) - 1);
    req.title[sizeof(req.title) - 1] = '\0';

    if (xQueueSend(_webhookQueue, &req, pdMS_TO_TICKS(10)) == pdTRUE) {
        return true;
    }
    DBG("Notif", "Webhook queue full, dropping Discord message");
    return false;
}

bool NotificationService::enqueueGenericWebhook(const String& payload) {
    if (!_webhookQueue) return false;

    // FIX #12: Reject overlength payloads instead of truncating into invalid JSON
    if (payload.length() >= sizeof(WebhookRequest::payload)) {
        DBG("Notif", "Webhook payload too long (%d bytes), dropping", payload.length());
        return false;
    }

    WebhookRequest req;
    req.type = WebhookType::GENERIC;
    strncpy(req.url, _config.generic_webhook, sizeof(req.url) - 1);
    req.url[sizeof(req.url) - 1] = '\0';
    strncpy(req.payload, payload.c_str(), sizeof(req.payload) - 1);
    req.payload[sizeof(req.payload) - 1] = '\0';
    req.title[0] = '\0';

    if (xQueueSend(_webhookQueue, &req, pdMS_TO_TICKS(10)) == pdTRUE) {
        return true;
    }
    DBG("Notif", "Webhook queue full, dropping generic message");
    return false;
}

bool NotificationService::sendTelegram(const String& message) {
    if (_telegramService) {
        return _telegramService->sendMessage(message);
    }
    DBG("Notif", "No TelegramService configured");
    return false;
}

String NotificationService::formatMessage(NotificationType type, const String& message, const String& details) {
    String result;
    result.reserve(256);
    result += "🔔 *";
    result += getTypeString(type);
    result += "*\n\n";
    result += message;

    if (details.length() > 0) {
        result += "\n\n📋 Details:\n";
        result += details;
    }

    result += "\n\n📱 Device: ";
    result += _deviceName;
    result += " (";
    result += ETH.macAddress();
    result += ")";
    result += "\n🌐 IP: ";
    result += ETH.localIP().toString();
    result += "\n⏰ Uptime: ";
    result += String(millis() / 1000);
    result += "s";

    return result;
}

bool NotificationService::checkCooldown(NotificationType type) {
    unsigned long now = millis();
    unsigned long lastTime = _lastNotification[(int)type];

    if (lastTime == 0) return true;  // First notification

    // Special cooldown for WiFi Anomaly (2 hours) to prevent spam
    unsigned long requiredCooldown = _config.cooldown_ms;
    if (type == NotificationType::WIFI_ANOMALY) {
        requiredCooldown = 7200000; // 2 hours
    }

    return (now - lastTime >= requiredCooldown);
}

const char* NotificationService::getTypeString(NotificationType type) {
    switch(type) {
        case NotificationType::TAMPER_ALERT: return "TAMPER ALERT";
        case NotificationType::PRESENCE_DETECTED: return "Presence Detected";
        case NotificationType::PRESENCE_CLEARED: return "Presence Cleared";
        case NotificationType::SYSTEM_ERROR: return "System Error";
        case NotificationType::WIFI_ANOMALY: return "WiFi Anomaly";
        case NotificationType::HEALTH_WARNING: return "Health Warning";
        case NotificationType::ALARM_STATE_CHANGE: return "Alarm State Change";
        case NotificationType::ENTRY_DETECTED: return "Entry Detected";
        case NotificationType::ALARM_TRIGGERED: return "ALARM TRIGGERED";
        default: return "Unknown";
    }
}

// Configuration setters
void NotificationService::setTelegramConfig(const char* token, const char* chatId) {
    strncpy(_config.telegram_token, token, 63);
    _config.telegram_token[63] = '\0';
    strncpy(_config.telegram_chat_id, chatId, 31);
    _config.telegram_chat_id[31] = '\0';

    _prefs->putString("tg_token", _config.telegram_token);
    _prefs->putString("tg_chat", _config.telegram_chat_id);
}

void NotificationService::setDiscordWebhook(const char* webhook) {
    strncpy(_config.discord_webhook, webhook, 255);
    _config.discord_webhook[255] = '\0';
    _prefs->putString("dc_webhook", _config.discord_webhook);
}

void NotificationService::setGenericWebhook(const char* webhook) {
    strncpy(_config.generic_webhook, webhook, 255);
    _config.generic_webhook[255] = '\0';
    _prefs->putString("gen_webhook", _config.generic_webhook);
}

void NotificationService::setEnabled(bool enabled) {
    _config.enabled = enabled;
    _prefs->putBool("notif_enabled", enabled);
}

void NotificationService::setCooldown(unsigned long ms) {
    _config.cooldown_ms = ms;
    _prefs->putULong("notif_cooldown", ms);
}
#endif
