#include "services/LogService.h"

LogService::LogService(size_t maxEntries) : _maxEntries(maxEntries), _head(0), _count(0) {
    _buffer = new LogEntry[_maxEntries];
    _mutex = xSemaphoreCreateMutex();
}

LogService::~LogService() {
    delete[] _buffer;
}

void LogService::log(const String& type, const String& message) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    size_t index = (_head + _count) % _maxEntries;

    if (_count < _maxEntries) {
        _count++;
    } else {
        // Buffer full, overwrite oldest
        index = _head;
        _head = (_head + 1) % _maxEntries;
    }

    _buffer[index].timestamp = millis() / 1000;
    strncpy(_buffer[index].type, type.c_str(), sizeof(_buffer[index].type) - 1);
    _buffer[index].type[sizeof(_buffer[index].type) - 1] = '\0';
    strncpy(_buffer[index].message, message.c_str(), sizeof(_buffer[index].message) - 1);
    _buffer[index].message[sizeof(_buffer[index].message) - 1] = '\0';

    if (_mutex) xSemaphoreGive(_mutex);

    Serial.printf("[%s] %s\n", type.c_str(), message.c_str());
}

void LogService::getLogJSON(JsonDocument& doc) {
    JsonArray arr = doc.to<JsonArray>();

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    for (size_t i = 0; i < _count; i++) {
        size_t logical_idx = _count - 1 - i;
        size_t physical_idx = (_head + logical_idx) % _maxEntries;

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"] = _buffer[physical_idx].timestamp;
        obj["type"] = _buffer[physical_idx].type;
        obj["msg"] = _buffer[physical_idx].message;
    }

    if (_mutex) xSemaphoreGive(_mutex);
}

void LogService::clear() {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _head = 0;
    _count = 0;
    if (_mutex) xSemaphoreGive(_mutex);
}
