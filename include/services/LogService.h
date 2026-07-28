#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "services/LogRing.h"

class LogService {
public:
    LogService(size_t maxEntries = 20);
    ~LogService();
    LogService(const LogService&) = delete;         // vlastní _buffer (new[])
    LogService& operator=(const LogService&) = delete;

    void log(const String& type, const String& message);
    void info(const String& message) { log("INFO", message); }
    void warn(const String& message) { log("WARN", message); }
    void error(const String& message) { log("ERROR", message); }
    void alarm(const String& message) { log("ALARM", message); }

    void getLogJSON(JsonDocument& doc);
    void clear();

    // RTC-noinit mirror: every appended entry is also written here so the
    // log survives panic/software reset (not power loss). May stay nullptr.
    void attachRtcMirror(LogRtcRing* ring) { _rtcMirror = ring; }
    // Preload entries captured before the previous reboot (oldest-first).
    void restorePrevBoot(const LogRtcRing& ring);

private:
    LogRtcRing* _rtcMirror = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    LogEntry* _buffer;
    size_t _maxEntries;
    size_t _head;   // Index of oldest element
    size_t _count;
};

#endif
