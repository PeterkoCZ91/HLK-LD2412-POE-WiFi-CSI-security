#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
#include "services/SensitiveDataRedaction.h"

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif

// Ring buffer for remote debug log access via /api/debug
class DebugLog {
public:
    static constexpr size_t BUF_SIZE = 4096;

    static DebugLog& instance() {
        static DebugLog inst;
        return inst;
    }

    void append(const char* msg) {
        size_t len = strlen(msg);
        portENTER_CRITICAL(&_mux);
        for (size_t i = 0; i < len && i < BUF_SIZE - 1; i++) {
            _buf[_head] = msg[i];
            _head = (_head + 1) % BUF_SIZE;
            if (_count < BUF_SIZE) _count++; else _tail = (_tail + 1) % BUF_SIZE;
        }
        // Ensure newline
        if (len > 0 && msg[len - 1] != '\n') {
            _buf[_head] = '\n';
            _head = (_head + 1) % BUF_SIZE;
            if (_count < BUF_SIZE) _count++; else _tail = (_tail + 1) % BUF_SIZE;
        }
        portEXIT_CRITICAL(&_mux);
    }

    String read() const {
        String out;
        size_t count;
        portENTER_CRITICAL(&_mux);
        count = _count;
        portEXIT_CRITICAL(&_mux);
        if (!out.reserve(count)) return out;
        portENTER_CRITICAL(&_mux);
        count = _count < count ? _count : count;
        for (size_t i = 0; i < count; i++) {
            out += _buf[(_tail + i) % BUF_SIZE];
        }
        portEXIT_CRITICAL(&_mux);
        return out;
    }

    void clear() {
        portENTER_CRITICAL(&_mux);
        _head = 0; _tail = 0; _count = 0;
        portEXIT_CRITICAL(&_mux);
    }

private:
    DebugLog() : _head(0), _tail(0), _count(0) { memset(_buf, 0, BUF_SIZE); }
    char _buf[BUF_SIZE];
    size_t _head, _tail, _count;
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

// DBG macro: serial + ring buffer
#if SERIAL_DEBUG
  #define DBG(tag, fmt, ...) do { \
      char _dbg_buf[192]; \
      snprintf(_dbg_buf, sizeof(_dbg_buf), "[" tag "] " fmt, ##__VA_ARGS__); \
      redactSensitiveText(_dbg_buf, sizeof(_dbg_buf)); \
      Serial.println(_dbg_buf); \
      DebugLog::instance().append(_dbg_buf); \
  } while(0)
#else
  #define DBG(tag, fmt, ...) do { \
      char _dbg_buf[192]; \
      snprintf(_dbg_buf, sizeof(_dbg_buf), "[" tag "] " fmt, ##__VA_ARGS__); \
      redactSensitiveText(_dbg_buf, sizeof(_dbg_buf)); \
      DebugLog::instance().append(_dbg_buf); \
  } while(0)
#endif

#endif
