#ifndef KNOWN_DEVICES_H
#define KNOWN_DEVICES_H

struct DeviceIdentity {
    const char* mac;       // MAC address (lowercase)
    const char* id;        // MQTT Client ID
    const char* hostname;  // mDNS Hostname
};

// Empty table — no POE devices flashed yet
// Add after first flash:
// { "xx:xx:xx:xx:xx:xx", "poe2412_mistnost", "poe2412-mistnost" },
const DeviceIdentity KNOWN_DEVICES[] = {};

const int KNOWN_DEVICE_COUNT = sizeof(KNOWN_DEVICES) / sizeof(KNOWN_DEVICES[0]);

#endif
