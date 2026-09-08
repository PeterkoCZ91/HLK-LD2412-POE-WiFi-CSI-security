#pragma once
#include <atomic>
#include <Arduino.h>
#include "services/HeapMetrics.h"

// These are overlapping observation windows, not allocator call-site proof.
// A global watermark drop during TLS + HTTP belongs to BOTH windows.
enum class HeapActivity : uint8_t {
    HttpHealth, FeedbackWrite, FeedbackExport, MqttLoop, TelegramLoop, SsePublish, Count
};
struct HeapActivityStats {
    std::atomic<uint32_t> calls{0}, drops{0}, largestDrop{0}, lastMs{0};
};
inline HeapActivityStats g_heapActivities[(unsigned)HeapActivity::Count];
inline std::atomic<uint32_t> g_heapActivityMask{0};
inline const char* heapActivityName(unsigned i) {
    static const char* names[] = {"http_health", "feedback_write", "feedback_export",
                                 "mqtt_loop", "telegram_loop", "sse_publish"};
    return i < (unsigned)HeapActivity::Count ? names[i] : "unknown";
}
class HeapActivityScope {
public:
    explicit HeapActivityScope(HeapActivity activity) : _id((unsigned)activity),
        _before(heapMinFreeUsable()) {
        g_heapActivityMask.fetch_or(1U << _id);
        g_heapActivities[_id].calls.fetch_add(1);
    }
    ~HeapActivityScope() {
        uint32_t after = heapMinFreeUsable();
        auto& stat = g_heapActivities[_id];
        if (after < _before) {
            uint32_t drop = _before - after;
            stat.drops.fetch_add(1);
            stat.lastMs.store(millis());
            uint32_t old = stat.largestDrop.load();
            while (old < drop && !stat.largestDrop.compare_exchange_weak(old, drop)) {}
        }
        g_heapActivityMask.fetch_and(~(1U << _id));
    }
    HeapActivityScope(const HeapActivityScope&) = delete;
    HeapActivityScope& operator=(const HeapActivityScope&) = delete;
private:
    unsigned _id;
    uint32_t _before;
};
