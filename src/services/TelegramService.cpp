#ifndef LITE_BUILD
#include "services/TelegramService.h"
#include "services/SecurityMonitor.h"
#include "debug.h"
#include "secrets.h"
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
#include <ETH.h>
#include <inttypes.h>

TelegramService::TelegramService() : _bot(nullptr), _enabled(false), _connected(false),
                                      _lastCheck(0), _checkInterval(5000), _radar(nullptr), _muteStartTime(0), _muteDuration(0) {
    _chatId[0] = '\0';
    _sendMutex = xSemaphoreCreateMutex();
}

void TelegramService::begin(Preferences* prefs) {
    _prefs = prefs;

    // Load configuration (with defaults from secrets.h)
    _enabled = _prefs->getBool("tg_direct_en", false);
    String token = _prefs->getString("tg_token", TELEGRAM_TOKEN_DEFAULT);
    String chatId = _prefs->getString("tg_chat", TELEGRAM_CHAT_ID_DEFAULT);

    // Persist or update defaults from secrets.h to NVS
    if (strlen(TELEGRAM_TOKEN_DEFAULT) > 0) {
        if (!_prefs->isKey("tg_token")) {
            token = TELEGRAM_TOKEN_DEFAULT;
            chatId = TELEGRAM_CHAT_ID_DEFAULT;
            _prefs->putString("tg_token", token);
            _prefs->putString("tg_chat", chatId);
            DBG("Telegram", "NVS updated with compiled defaults");
        }
    }

    token.toCharArray(_token, 50);
    chatId.toCharArray(_chatId, 20);

    if (_enabled && strlen(_token) > 10 && strlen(_chatId) > 0) {
        _client.setInsecure();
        _bot = new AsyncTelegram2(_client);
        _bot->setUpdateTime(5000);
        _bot->setTelegramToken(_token);

        // Create send queue and background task
        _sendQueue = xQueueCreate(QUEUE_SIZE, sizeof(TelegramQueueItem));
        if (_sendQueue) {
            xTaskCreatePinnedToCore(telegramTaskFunc, "tg_task", 10240, this, 1, &_taskHandle, 0);
            DBG("Telegram", "Background task started");
        }

        DBG("Telegram", "Direct mode enabled");
        _connected = true;
    } else {
        DBG("Telegram", "Direct mode disabled");
    }
}

// Background task — handles both sending queued messages and polling for new ones
void TelegramService::telegramTaskFunc(void* param) {
    TelegramService* self = (TelegramService*)param;
    self->telegramLoop();
}

void TelegramService::telegramLoop() {
    TelegramQueueItem item;

    for (;;) {
        // 1. Process outgoing message queue (non-blocking, 100ms wait)
        if (_sendQueue && xQueueReceive(_sendQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (ETH.linkUp() && _bot) {
                bool ok = sendMessageDirect(String(item.text));
                if (!ok && item.retries < 3) {
                    item.retries++;
                    xQueueSendToFront(_sendQueue, &item, pdMS_TO_TICKS(10));
                    vTaskDelay(pdMS_TO_TICKS(3000));
                } else if (!ok) {
                    DBG("Telegram", "Message permanently dropped after %d retries", item.retries);
                }
            } else {
                // WiFi down — put back in queue for later
                if (item.retries < 5) {
                    item.retries++;
                    xQueueSendToFront(_sendQueue, &item, pdMS_TO_TICKS(10));
                    vTaskDelay(pdMS_TO_TICKS(5000));
                } else {
                    DBG("Telegram", "Message dropped (offline) after %d retries", item.retries);
                }
            }
            continue; // Prioritize sending over polling
        }

        // 2. Poll for incoming commands
        if (_enabled && _connected && _bot && ETH.linkUp()) {
            unsigned long now = millis();
            if (now - _lastCheck > _checkInterval) {
                _lastCheck = now;
                handleNewMessages();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void TelegramService::update() {
    // Polling and sending moved to background task — nothing to do here
}

// Non-blocking: enqueue message for background task
bool TelegramService::sendMessage(const String& text) {
    if (!_enabled || !_connected) {
        DBG("Telegram", "Not enabled or not connected");
        return false;
    }

    if (!_sendQueue) {
        DBG("Telegram", "Queue not initialized");
        return false;
    }

    TelegramQueueItem item;
    strncpy(item.text, text.c_str(), sizeof(item.text) - 1);
    item.text[sizeof(item.text) - 1] = '\0';

    if (xQueueSend(_sendQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
        DBG("Telegram", "Queued: %.40s...", text.c_str());
        return true;
    }

    _droppedMessages++;
    DBG("Telegram", "Queue full, dropping message (#%d total)", _droppedMessages);
    return false;
}

// Actual blocking send — called from background task or directly for diagnostics
bool TelegramService::sendMessageDirect(const String& text) {
    if (!_bot || strlen(_chatId) == 0) return false;

    if (!_sendMutex || xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        DBG("Telegram", "Mutex timeout, skipping send");
        return false;
    }

    DBG("Telegram", "Sending: %.60s...", text.c_str());

    int64_t chatIdNum = strtoll(_chatId, nullptr, 10);

    TBMessage msg;
    msg.chatId = chatIdNum;

    // wait=true → blocking send, waits for Telegram API response
    // Without wait=true, AsyncTelegram2 sends but returns false (no response check)
    bool result = _bot->sendMessage(msg, text.c_str(), nullptr, true);

    xSemaphoreGive(_sendMutex);

    if (result) {
        DBG("Telegram", "Message sent successfully");
    } else {
        DBG("Telegram", "Failed to send message");
    }

    return result;
}

bool TelegramService::sendAlert(const String& title, const String& details) {
    if (_muteDuration > 0 && (millis() - _muteStartTime) < _muteDuration) {
        DBG("Telegram", "Alert muted");
        return false;
    }

    String message = "🚨 *" + title + "*\n\n";
    if (details.length() > 0) {
        message += details;
    }
    message += "\n\n_" + String(millis() / 1000) + "s uptime_";

    return sendMessage(message);
}

void TelegramService::handleNewMessages() {
    if (!_bot) return;

    TBMessage msg;
    MessageType mt = _bot->getNewMessage(msg);
    if (mt != MessageNoData) {
        // Use PRId64 for correct int64_t to string conversion (group chat IDs are negative)
        char chatIdBuf[21];
        snprintf(chatIdBuf, sizeof(chatIdBuf), "%" PRId64, msg.chatId);
        String text = msg.text;

        DBG("Telegram", "Received from %s: %s", chatIdBuf, text.c_str());

        if (strcmp(chatIdBuf, _chatId) == 0) {
            processCommand(text, String(chatIdBuf));
        } else {
            DBG("Telegram", "Ignoring message from unknown chat: %s", chatIdBuf);
        }
    }
}

void TelegramService::processCommand(const String& command, const String& chatId) {
    // Strip @BotName suffix for group chat commands
    String cmd = command;
    int atPos = cmd.indexOf('@');
    if (atPos > 0) cmd = cmd.substring(0, atPos);

    if (cmd == "/start") {
        sendMessage("👋 *LD2412 Security Node* " + String(FW_VERSION) + "\n\nCommands:\n/status - System status\n/arm - Arm alarm\n/disarm - Disarm alarm\n/arm_now - Immediate arm\n/learn - Learn static reflector\n/light - Light level\n/mute - Mute 10 min\n/restart - Restart");
    }
    else if (cmd == "/arm") {
        if (_secMon) {
            _secMon->setArmed(true, false);
            // SecurityMonitor sends notification via triggerAlert
        } else {
            sendMessage("❌ SecurityMonitor unavailable");
        }
    }
    else if (cmd == "/arm_now") {
        if (_secMon) {
            _secMon->setArmed(true, true);
        } else {
            sendMessage("❌ SecurityMonitor unavailable");
        }
    }
    else if (cmd == "/disarm") {
        if (_secMon) {
            _secMon->setArmed(false);
        } else {
            sendMessage("❌ SecurityMonitor unavailable");
        }
    }
    else if (cmd == "/status") {
        String msg = "📊 *Status Report*\n\n";

        if (_secMon) {
            msg += "🛡️ *Alarm:* " + String(_secMon->getAlarmStateStr());
            String zone = _secMon->getCurrentZoneName();
            if (zone.length() > 0 && zone != "none")
                msg += " _(zone: " + zone + ")_";
            msg += "\n\n";
        }

        if (_radar) {
            RadarData d = _radar->getData();
            JsonDocument doc;
            _radar->getTelemetryJson(doc);

            msg += "📡 *Radar*\n";
            String stateStr = "Klid";
            if (d.state == PresenceState::PRESENCE_DETECTED) stateStr = "🔴 DETEKCE";
            else if (d.state == PresenceState::HOLD_TIMEOUT)  stateStr = "⏳ HOLD";
            else if (d.state == PresenceState::TAMPER)         stateStr = "🚨 TAMPER";
            msg += "Stav: *" + stateStr + "*\n";
            msg += "Distance: " + String(d.distance_cm) + " cm\n";
            msg += "Energie Mov/Stat: " + String(d.moving_energy) + " / " + String(d.static_energy) + "%\n";

            int sg = doc["static_gate"]  | -1;
            int mg = doc["moving_gate"]  | -1;
            if (sg >= 0) msg += "Static gate: " + String(sg) + " (~" + String(sg * 75) + "cm)\n";
            if (mg >= 0) msg += "Moving gate: " + String(mg) + " (~" + String(mg * 75) + "cm)\n";

            msg += "UART: " + String(doc["uart_state"].as<const char*>() ? doc["uart_state"].as<const char*>() : "?");
            float fr = doc["frame_rate"] | 0.0f;
            msg += " | " + String(fr, 1) + " fps\n\n";
        }

        msg += "📶 *Network*\n";
        msg += "IP: " + ETH.localIP().toString() + "\n";
        msg += "ETH: " + String(ETH.linkUp() ? "UP" : "DOWN") + " " + String(ETH.linkSpeed()) + "Mbps\n\n";

        msg += "⚙️ *Info*\n";
        msg += "FW: " + String(FW_VERSION) + "\n";
        msg += "Uptime: " + String(millis() / 60000) + " min\n";
        msg += "Heap: " + String(ESP.getFreeHeap() / 1024) + " kB\n";

        sendMessage(msg);
    }
    else if (cmd == "/light") {
        if (_radar && _radar->isEngineeringMode()) {
            sendMessage("💡 Light level: *" + String(_radar->getLightLevel()) + "* (0-255)");
        } else {
            sendMessage("⚠️ Engineering Mode must be active for light reading (/eng_on).");
        }
    }
    else if (cmd == "/eng_on") {
        if (_radar && _radar->setEngineeringMode(true)) sendMessage("✅ Engineering Mode ZAPNUT");
        else sendMessage("❌ Error enabling");
    }
    else if (cmd == "/eng_off") {
        if (_radar && _radar->setEngineeringMode(false)) sendMessage("✅ Engineering Mode VYPNUT");
        else sendMessage("❌ Error disabling");
    }
    else if (cmd == "/restart") {
        sendMessage("🔄 Restarting...");
        if (_shouldReboot) *_shouldReboot = true;
    }
    else if (cmd == "/learn") {
        if (_radar) {
            if (_radar->isLearning()) {
                JsonDocument doc;
                _radar->getLearnResultJson(doc);
                int pct  = doc["progress"] | 0;
                int freq = doc["static_freq_pct"] | 0;
                int gate = doc["top_gate"] | 0;
                sendMessage("⏳ Learn in progress: " + String(pct) + "% | Static: " + String(freq) + "% | Top gate: " + String(gate) + " (~" + String(gate*75) + "cm)");
            } else {
                bool started = _radar->startStaticLearn(180);
                if (started)
                    sendMessage("📡 Static learn started (3 min). Results will be sent automatically.");
                else
                    sendMessage("❌ Learn failed to start.");
            }
        }
    }
    else if (cmd == "/mute") {
        _muteStartTime = millis();
        _muteDuration = 600000; // 10 minutes
        sendMessage("🔕 Notifikace ztlumeny na 10 minut.");
    }
    else if (cmd == "/unmute") {
        _muteDuration = 0;
        sendMessage("🔔 Notifikace zapnuty.");
    }
    else if (cmd == "/help") {
        sendMessage("ℹ️ *Commands*\n\n/status - Detailed status (FW, UART, gate)\n/arm - Arm alarm (s delay)\n/arm_now - Immediate arm\n/disarm - Disarm alarm\n/learn - Learn static reflector (3 min)\n/light - Light sensor\n/mute - Mute for 10 min\n/unmute - Unmute\n/eng_on - Enable Eng. mode\n/eng_off - Disable Eng. mode\n/restart - Restart");
    }
    else {
        sendMessage("❓ Unknown command. Try /help");
    }
}

void TelegramService::setEnabled(bool enabled) {
    _enabled = enabled;
    _prefs->putBool("tg_direct_en", enabled);
}

void TelegramService::setToken(const char* token) {
    strncpy(_token, token, 49);
    _token[49] = '\0';
    _prefs->putString("tg_token", token);
}

void TelegramService::setChatId(const char* chatId) {
    strncpy(_chatId, chatId, 19);
    _chatId[19] = '\0';
    _prefs->putString("tg_chat", chatId);
}
#endif
