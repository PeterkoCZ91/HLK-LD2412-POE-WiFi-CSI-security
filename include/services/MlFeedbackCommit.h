#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <algorithm>

// File-like API, shared by LittleFS and the fault-injection native tests.
// Single owner only. Filesystem rename must atomically replace an existing file.
template<class Fs, class Header, class Sample>
bool commitFeedbackFile(Fs& fs, const char* name, const char* staging,
                        const Header& header, const Sample& sample,
                        uint32_t capacity, uint32_t slot, bool requireSource) {
    auto source = fs.open(name, "r");
    if (!source && requireSource) return false;
    auto target = fs.open(staging, "w");
    if (!target) { source.close(); return false; }
    bool ok = target.write((const uint8_t*)&header, sizeof(header)) == sizeof(header);
    if (source && !source.seek(sizeof(header))) ok = false;
    Sample chunk[8]{};
    for (uint32_t base = 0; ok && base < capacity; base += 8) {
        size_t bytes = std::min<uint32_t>(8, capacity - base) * sizeof(Sample);
        if (source) ok = source.read((uint8_t*)chunk, bytes) == bytes;
        else memset(chunk, 0, bytes);
        if (slot >= base && slot < base + bytes / sizeof(Sample)) chunk[slot - base] = sample;
        if (ok) ok = target.write((const uint8_t*)chunk, bytes) == bytes;
    }
    source.close();
    target.flush();
    target.close();
    auto verify = fs.open(staging, "r");
    Header actual{};
    ok = ok && verify && verify.size() == sizeof(Header) + capacity * sizeof(Sample);
    ok = ok && verify.read((uint8_t*)&actual, sizeof(actual)) == sizeof(actual) &&
        memcmp(&header, &actual, sizeof(header)) == 0;
    Sample written{};
    ok = ok && verify.seek(sizeof(Header) + slot * sizeof(Sample)) &&
        verify.read((uint8_t*)&written, sizeof(written)) == sizeof(written) &&
        memcmp(&sample, &written, sizeof(sample)) == 0;
    verify.close();
    if (!ok || !fs.rename(staging, name)) {
        fs.remove(staging);
        return false;
    }
    return true;
}
