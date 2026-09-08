#ifndef GATED_WEB_SERVER_H
#define GATED_WEB_SERVER_H

#include <new>
#include <type_traits>
#include <ESPAsyncWebServer.h>
#include "services/HeapGatePolicy.h"
#include "services/WebAdmissionPolicy.h"
#include "services/HeapMetrics.h"

// dev7 L1: AsyncWebServer whose accept path consults the low-heap gate before
// any allocation happens. The base-class constructor installs an onClient
// callback that builds the request object with a THROWING `new` (its
// `if (r == NULL)` check is dead code) — under real heap exhaustion the accept
// itself panics async_tcp, and the header parser right behind it did exactly
// that in the 2026-08-15 field coredump. AsyncServer::onClient() overwrites
// the stored callback, so re-registering here replaces the library's accept
// logic without forking it.
//
// The lambda mirrors the library's (ESPAsyncWebServer pinned commit f5205596,
// WebServer.cpp:44-56) with three changes: the gate check, the concurrent
// admission check, and new(std::nothrow).
// Re-verify that block when bumping the library pin in platformio.ini.
//
// dev17: the gate alone was not enough. It reads the heap at accept time, but a
// connection's real cost lands milliseconds later, so a burst of 15-20 parallel
// requests was admitted in full against one healthy reading and then ran the
// heap to 7.6 kB free / 6.6 kB largest — panic. WebAdmissionPolicy bounds how
// many connections may be in flight at once and charges each admitted one a
// heap reserve; see the header for the bench numbers behind the thresholds.
class GatedAsyncWebServer : public AsyncWebServer {
public:
    GatedAsyncWebServer(uint16_t port, HeapGatePolicy& gate)
        : AsyncWebServer(port), _gate(gate) {
        s_active = this;
        _server.onClient([](void* s, AsyncClient* c) {
            auto* self = static_cast<GatedAsyncWebServer*>(s);
            if (c == nullptr) return;

            const uint32_t freeB    = heapFreeUsable();
            const uint32_t largestB = heapLargestUsable();
            // The gate keeps its own hysteresis and reject counters — evaluate
            // it first so its telemetry keeps its dev7 meaning.
            const bool gateClosed = !self->_gate.shouldAccept(freeB, largestB);
            // One source of truth for the floor: whatever the gate closes at.
            self->_admission.setFloorBytes(self->_gate.config().closeFreeBytes);
            const WebAdmitResult adm =
                self->_admission.admit((uint32_t)millis(), freeB, gateClosed);
            if (!adm.accepted()) {
                // dev21 (field coredump 2026-09-06, bench dev7): AsyncClient::_error()
                // use-after-free crash traced to ESP32Async/AsyncTCP#118 — our
                // then-pinned v3.4.10 abort() never purged the async event queue
                // for the client being aborted, so a queued LWIP_TCP_ERROR (or
                // any other pending event, common under a burst of short-lived
                // connections — exactly what a high-rejection-rate admission
                // policy produces) could still fire on this object after
                // `delete c` freed it. A same-shaped bug was independently found,
                // fixed and released upstream in AsyncTCP v3.5.0 (merged 2026-07-21,
                // months after our pin): abort() now synchronously purges the
                // queue and runs _error(ERR_ABRT) before returning, so by the
                // time we delete c below nothing can still reference it. See
                // platformio.ini and docs/RELEASE_5.7.1_VALIDATION.md. A hand-rolled
                // firmware-side workaround was tried first and tested unreliable
                // (see doc) — fixing it at the actual source was correct instead.
                c->abort();    // alloc-free TCP RST — same rationale as the dev6 OOM guards
                delete c;
                return;
            }

            c->setRxTimeout(3);
            AsyncWebServerRequest* r = new (std::nothrow) AsyncWebServerRequest(self, c);
            if (r == nullptr) {
                self->_admission.release(adm.slot);
                c->abort();
                delete c;
                return;
            }

            // Release the admission slot when the connection ends. The closure
            // captures ONLY the 16-bit slot: libstdc++ keeps a std::function's
            // target inline while it is trivially copyable and fits the 8-byte
            // _Nocopy_types buffer, and an out-of-line target would mean a
            // THROWING operator new on the exact path that has to survive heap
            // exhaustion. The server is reached through s_active instead of a
            // captured `this` to keep the closure at 2 bytes.
            //
            // This is a FAST path, not a guarantee — the policy's slot TTL is
            // what actually bounds a slot's life. Two known cases never reach
            // this closure: an SSE stream, whose AsyncEventSourceClient takes
            // the raw AsyncClient's disconnect callback away from the request
            // (AsyncEventSource.cpp:179), and request->abort(), which reaches
            // lwIP directly and reports back only through an AsyncTCP event
            // packet allocated with new(std::nothrow) — the allocation that
            // fails first under the very heap exhaustion this guards against.
            const uint16_t slot = adm.slot;
            auto onGone = [slot]() { _releaseSlot(slot); };
            static_assert(sizeof(onGone) <= sizeof(void*) &&
                          std::is_trivially_copyable<decltype(onGone)>::value,
                          "disconnect closure must stay inside std::function's inline buffer");
            r->onDisconnect(onGone);
        }, this);
    }

    // Read-only view for /api/health (WebRoutes.cpp). Never null-checked there
    // via this object — use active().
    const WebAdmissionPolicy& admission() const { return _admission; }

    // The single server instance of this firmware, or nullptr before setup().
    static GatedAsyncWebServer* active() { return s_active; }

private:
    // Static hop so the disconnect closure needs no captured `this`.
    static void _releaseSlot(uint16_t slot) {
        if (s_active) s_active->_admission.release(slot);
    }

    HeapGatePolicy&    _gate;
    WebAdmissionPolicy _admission;

    inline static GatedAsyncWebServer* s_active = nullptr;
};

#endif // GATED_WEB_SERVER_H
