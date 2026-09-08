#include <unity.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "services/MlFeedbackCommit.h"

struct MemoryFs {
    using Bytes = std::vector<uint8_t>;
    std::map<std::string, std::shared_ptr<Bytes>> files;
    int failAt = -1, writes = 0;
    bool failRename = false, corrupt = false;
    struct File {
        MemoryFs* fs;
        std::shared_ptr<Bytes> data;
        size_t pos = 0;
        explicit operator bool() const { return bool(data); }
        size_t size() const { return data ? data->size() : 0; }
        bool seek(size_t offset) { pos = offset; return bool(data); }
        size_t read(uint8_t* out, size_t n) {
            if (!data || pos + n > data->size()) return 0;
            memcpy(out, data->data() + pos, n); pos += n; return n;
        }
        size_t write(const uint8_t* in, size_t n) {
            if (!data) return 0;
            if (fs->writes++ == fs->failAt) n /= 2;
            if (pos + n > data->size()) data->resize(pos + n);
            memcpy(data->data() + pos, in, n); pos += n; return n;
        }
        void flush() {}
        void close() { data.reset(); }
    };
    File open(const char* path, const char* mode) {
        std::string name(path);
        if (mode[0] == 'w') files[name] = std::make_shared<Bytes>();
        auto it = files.find(name);
        if (it == files.end()) return {this, nullptr};
        if (corrupt && name == "tmp" && mode[0] == 'r' && !it->second->empty())
            it->second->at(0) ^= 1;
        return {this, it->second};
    }
    bool rename(const char* from, const char* to) {
        if (failRename) return false;
        files[to] = files.at(from); files.erase(from); return true;
    }
    bool remove(const char* path) { return files.erase(path) != 0; }
};
struct Header { uint32_t generation; };
struct Sample { float features[17]; uint32_t stamp; uint8_t label; };
void setUp() {}
void tearDown() {}

MemoryFs baseline() {
    MemoryFs fs;
    Header h{1}; Sample s{}; s.label = 1; s.features[0] = 42;
    bool ok = commitFeedbackFile(fs, "live", "tmp", h, s, 17, 0, false);
    if (!ok) abort();
    fs.writes = 0;
    return fs;
}

void test_failures_preserve_old_file() {
    // Header, each data chunk, verification corruption, and commit failure.
    for (int point = 0; point < 6; ++point) {
        auto fs = baseline();
        auto old = *fs.files.at("live");
        fs.failAt = point < 4 ? point : -1;
        fs.corrupt = point == 4;
        fs.failRename = point == 5;
        Header h{2}; Sample s{}; s.features[0] = 99;
        TEST_ASSERT_FALSE(commitFeedbackFile(fs, "live", "tmp", h, s, 17, 0, true));
        TEST_ASSERT_TRUE(old == *fs.files.at("live"));
        TEST_ASSERT_TRUE(fs.files.find("tmp") == fs.files.end());
    }
}

void test_success_replaces_only_chosen_slot_and_header() {
    auto fs = baseline();
    Header h{2}; Sample s{}; s.features[0] = 99; s.label = 1;
    TEST_ASSERT_TRUE(commitFeedbackFile(fs, "live", "tmp", h, s, 17, 16, true));
    auto f = fs.open("live", "r");
    Header actual{}; Sample first{}, last{};
    TEST_ASSERT_EQUAL(sizeof(actual), f.read((uint8_t*)&actual, sizeof(actual)));
    TEST_ASSERT_EQUAL(2, actual.generation);
    f.read((uint8_t*)&first, sizeof(first));
    TEST_ASSERT_EQUAL_FLOAT(42, first.features[0]);
    f.seek(sizeof(Header) + 16 * sizeof(Sample));
    f.read((uint8_t*)&last, sizeof(last));
    TEST_ASSERT_EQUAL_MEMORY(&s, &last, sizeof(s));
}

void test_missing_source_never_resets_existing_ring() {
    MemoryFs fs;
    Header h{2}; Sample s{};
    TEST_ASSERT_FALSE(commitFeedbackFile(fs, "live", "tmp", h, s, 17, 0, true));
    TEST_ASSERT_TRUE(fs.files.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_failures_preserve_old_file);
    RUN_TEST(test_success_replaces_only_chosen_slot_and_header);
    RUN_TEST(test_missing_source_never_resets_existing_ring);
    return UNITY_END();
}
