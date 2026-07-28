#ifndef OTA_TLS_TRUST_POLICY_H
#define OTA_TLS_TRUST_POLICY_H

#include <stddef.h>
#include <string.h>

// OTA pull must never silently downgrade HTTPS to an unauthenticated channel.
// A single PEM CA keeps the trust anchor explicit and lets WiFiClientSecure
// perform normal certificate-chain and hostname verification.
static constexpr size_t OTA_TLS_CA_MAX_LEN = 3072;

inline bool otaTlsPemValid(const char* pem) {
    if (pem == nullptr) return false;
    const size_t len = strlen(pem);
    if (len == 0 || len > OTA_TLS_CA_MAX_LEN) return false;
    const char* begin = "-----BEGIN CERTIFICATE-----";
    const char* end = "-----END CERTIFICATE-----";
    const char* first = strstr(pem, begin);
    const char* last = strstr(pem, end);
    return first == pem && last != nullptr && last > first &&
           last + strlen(end) <= pem + len;
}

inline bool otaTlsHttpsUrl(const char* url) {
    return url != nullptr && strncmp(url, "https://", 8) == 0;
}

inline bool otaTlsHttpUrl(const char* url) {
    return url != nullptr && strncmp(url, "http://", 7) == 0;
}

inline bool otaTlsSupportedUrl(const char* url) {
    return otaTlsHttpUrl(url) || otaTlsHttpsUrl(url);
}

#endif
