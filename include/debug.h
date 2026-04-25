#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

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
    }

    String read() const {
        String out;
        out.reserve(_count);
        for (size_t i = 0; i < _count; i++) {
            out += _buf[(_tail + i) % BUF_SIZE];
        }
        return out;
    }

    void clear() { _head = 0; _tail = 0; _count = 0; }

private:
    DebugLog() : _head(0), _tail(0), _count(0) { memset(_buf, 0, BUF_SIZE); }
    char _buf[BUF_SIZE];
    size_t _head, _tail, _count;
};

// DBG macro: serial + ring buffer
#if SERIAL_DEBUG
  #define DBG(tag, fmt, ...) do { \
      char _dbg_buf[192]; \
      snprintf(_dbg_buf, sizeof(_dbg_buf), "[" tag "] " fmt, ##__VA_ARGS__); \
      Serial.println(_dbg_buf); \
      DebugLog::instance().append(_dbg_buf); \
  } while(0)
#else
  #define DBG(tag, fmt, ...) do { \
      char _dbg_buf[192]; \
      snprintf(_dbg_buf, sizeof(_dbg_buf), "[" tag "] " fmt, ##__VA_ARGS__); \
      DebugLog::instance().append(_dbg_buf); \
  } while(0)
#endif

#endif
