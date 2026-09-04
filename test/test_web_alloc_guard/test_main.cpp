// Source-invariant test for the web-handler OOM recovery paths.
// Run: pio test -e native -f test_web_alloc_guard
//
// WHY THIS EXISTS (coredump 2026-08-12): the async_tcp task panicked because an
// out-of-memory guard in sendJsonBuffered() recovered from a failed
// `new (std::nothrow)` by calling `request->send(503, ...)` — which itself
// allocates a response object via a *throwing* operator new. Under true heap
// exhaustion that second allocation also fails, libstdc++ can't build the
// bad_alloc, and std::terminate()/abort() reboots the node. The whole class of
// bug is invisible to the rest of the suite: `test_build_src = no` means src/ is
// never compiled natively, and WebRoutes.cpp depends on ESPAsyncWebServer
// (Arduino-only) anyway, so no behavioural native test can reach it. This test
// instead reads the source and enforces the invariant statically:
//
//   INVARIANT: every `new (std::nothrow)` failure branch in WebRoutes.cpp must
//   recover with an allocation-free path (request->abort()), never with an
//   allocating one (request->send(...) / request->beginResponse(...)).
//
// It is a lint-as-a-test: it would have failed on the pre-fix code and fails
// again on any regression that reintroduces an allocating OOM-recovery branch.

#include <unity.h>
#include <fstream>
#include <string>
#include <vector>

void setUp() {}
void tearDown() {}

// PlatformIO runs the native test binary from the project root; fall back to a
// few relatives so the test also works if invoked from a build subdir. Hard-fail
// (never silently pass) if the source can't be located.
static std::vector<std::string> readWebRoutesLines() {
    const char* candidates[] = {
        "src/WebRoutes.cpp",
        "../../src/WebRoutes.cpp",
        "../../../src/WebRoutes.cpp",
    };
    for (const char* path : candidates) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) lines.push_back(line);
        return lines;
    }
    return {};  // signalled as failure by the caller
}

static bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

void test_source_is_readable() {
    std::vector<std::string> lines = readWebRoutesLines();
    TEST_ASSERT_TRUE_MESSAGE(lines.size() > 100,
        "src/WebRoutes.cpp not found from the test CWD — fix the candidate paths");
}

// Every `new (std::nothrow)` must be followed (within the next 2 lines) by a
// nullptr guard that recovers with request->abort() and NOT request->send(...)
// or request->beginResponse(...).
void test_every_nothrow_guard_is_allocation_free() {
    std::vector<std::string> lines = readWebRoutesLines();
    TEST_ASSERT_TRUE_MESSAGE(!lines.empty(), "could not read WebRoutes.cpp");

    int nothrowSites = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (!contains(lines[i], "new (std::nothrow)")) continue;
        nothrowSites++;

        // Find the nullptr guard on this or the next couple of lines.
        std::string guard;
        for (size_t j = i; j < lines.size() && j <= i + 2; j++) {
            if (contains(lines[j], "== nullptr")) { guard = lines[j]; break; }
        }
        // Only inspect the code, not a trailing // comment (our own fix comment
        // legitimately mentions request->send() as the thing to avoid).
        size_t cpos = guard.find("//");
        if (cpos != std::string::npos) guard = guard.substr(0, cpos);
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "line %zu: new(nothrow) has no `== nullptr` guard within 2 lines",
                 i + 1);
        TEST_ASSERT_TRUE_MESSAGE(!guard.empty(), msg);

        // The recovery must be allocation-free.
        snprintf(msg, sizeof(msg),
                 "line %zu: OOM guard must call request->abort() (alloc-free)", i + 1);
        TEST_ASSERT_TRUE_MESSAGE(contains(guard, "request->abort()"), msg);

        snprintf(msg, sizeof(msg),
                 "line %zu: OOM guard must NOT call request->send() — it re-allocates and panics", i + 1);
        TEST_ASSERT_FALSE_MESSAGE(contains(guard, "request->send("), msg);

        snprintf(msg, sizeof(msg),
                 "line %zu: OOM guard must NOT call request->beginResponse() — it allocates", i + 1);
        TEST_ASSERT_FALSE_MESSAGE(contains(guard, "beginResponse("), msg);
    }

    // Guard against the scanner silently matching nothing (e.g. pattern renamed).
    TEST_ASSERT_TRUE_MESSAGE(nothrowSites >= 4,
        "expected >=4 new(std::nothrow) sites in WebRoutes.cpp; scanner may be stale");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_source_is_readable);
    RUN_TEST(test_every_nothrow_guard_is_allocation_free);
    return UNITY_END();
}
