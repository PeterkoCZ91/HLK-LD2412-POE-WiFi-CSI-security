#ifndef DMS_POLICY_H
#define DMS_POLICY_H

#include <stdint.h>

// v5.4.1 (navrh 2026-07-18, bod 2A): dms_count reset policy. The counter may
// only reset after a REAL publish succeeded since boot — _lastPublish is
// initialized to millis() in MQTTService::begin() to avoid a false DMS
// trigger, so "publish not stale" alone is true seconds after every boot and
// the DMS_MAX_RESTARTS cap never accumulated across the restart loop
// (observed: 8-10 consecutive dms_no_mqtt_publish resets on both test nodes
// during broker outages, ~31.5 min apart, degraded mode never reached).
inline bool dmsShouldResetCounter(uint8_t restarts, bool publishedSinceBoot,
                                  bool publishStale) {
    return restarts > 0 && publishedSinceBoot && !publishStale;
}

#endif
