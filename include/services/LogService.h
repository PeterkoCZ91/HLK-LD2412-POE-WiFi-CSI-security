#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>

struct LogEntry {
    unsigned long timestamp;
    char type[8];    // INFO, WARN, ERROR, ALARM
    char message[80];
};

class LogService {
public:
    LogService(size_t maxEntries = 20);
    ~LogService();

    void log(const String& type, const String& message);
    void info(const String& message) { log("INFO", message); }
    void warn(const String& message) { log("WARN", message); }
    void error(const String& message) { log("ERROR", message); }
    void alarm(const String& message) { log("ALARM", message); }

    void getLogJSON(JsonDocument& doc);
    void clear();

private:
    SemaphoreHandle_t _mutex = nullptr;
    LogEntry* _buffer;
    size_t _maxEntries;
    size_t _head;   // Index of oldest element
    size_t _count;
};

#endif
