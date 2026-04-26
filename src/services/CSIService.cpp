#include "services/CSIService.h"
#include "services/MQTTService.h"
#include "services/ml_features.h"
#include "services/ml_weights.h"
#include "debug.h"
#include <ETH.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_wifi.h"

// Breathing bandpass IIR coefficients (HP 0.08Hz + LP 0.6Hz @ ~100Hz)
static constexpr float BREATH_HP_B0 = 0.99749f;
static constexpr float BREATH_HP_A1 = -0.99498f;
static constexpr float BREATH_LP_B0 = 0.01850f;
static constexpr float BREATH_LP_A1 = -0.96300f;
static constexpr float BREATH_ENERGY_ALPHA = 0.00333f;

// Low-pass default: 11 Hz cutoff at 100 Hz sample rate
static constexpr float LOWPASS_CUTOFF_HZ = 11.0f;
static constexpr float LOWPASS_SAMPLE_RATE_HZ = 100.0f;

static const char* TAG = "CSI";

// Minimal DNS query for traffic generation (17 bytes)
static const uint8_t DNS_QUERY[] = {
    0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01
};

// ============================================================================
// Helpers
// ============================================================================

static float calculateMedian(float* arr, size_t size) {
    if (size == 0) return 0.0f;
    std::sort(arr, arr + size);
    if (size % 2 == 0) return (arr[size/2 - 1] + arr[size/2]) / 2.0f;
    return arr[size/2];
}

// ============================================================================
// Static callback trampoline
// ============================================================================

constexpr uint8_t CSIService::SUBCARRIERS[12];
std::atomic<bool> CSIService::_otaInProgress{false};

void CSIService::_csiCallback(void* ctx, wifi_csi_info_t* info) {
    if (ctx && info && info->buf && info->len > 0) {
        static_cast<CSIService*>(ctx)->_processCSI(info);
    }
}

// ============================================================================
// Constructor / Init
// ============================================================================

CSIService::CSIService() {}

void CSIService::_initWiFiForCSI(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);

    // Align WiFi hostname with the Ethernet one so mDNS / DHCP advertise a
    // single identity for the device. Must be called before WiFi.begin().
    const char* ethHost = ETH.getHostname();
    if (ethHost && ethHost[0] != '\0') {
        WiFi.setHostname(ethHost);
    }

    // Force 802.11n protocol for HT20 CSI (64 subcarriers)
    esp_err_t ret = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11N);
    if (ret != ESP_OK) {
        // Fallback to b/g/n
        ret = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        if (ret == ESP_OK) Serial.println("[CSI] 11n-only not accepted, using b/g/n fallback");
    }

    // Force HT20 bandwidth for consistent 64 subcarriers
    ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (ret != ESP_OK) {
        Serial.printf("[CSI] WARNING: Failed to set HT20 bandwidth: 0x%x\n", ret);
    }

    // Initialize internal WiFi structures for CSI (required even when false)
    esp_wifi_set_promiscuous(false);

    WiFi.begin(ssid, password);
    Serial.printf("[CSI] Connecting WiFi to %s (HT20/11n) for CSI capture...\n", ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[CSI] WARNING: WiFi not connected, will retry in background");
    } else {
        Serial.printf("[CSI] WiFi connected (IP: %s, RSSI: %d)\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    _restoreEthDefaultNetif();
}

// csi7: When CSI WiFi lands on the same subnet as Ethernet (flat LAN + router-hosted AP),
// lwIP's default_netif becomes WiFi after WiFi.begin(), which breaks outbound TCP from
// services expecting Ethernet (MQTT rc=-2, OTA, HTTP clients). Force Ethernet back as
// default — CSI RX is callback-driven and doesn't need to be default netif.
//
// Thread-context constraint: must be called from a non-TCPIP thread (main loop / Arduino
// setup / generic task). LOCK_TCPIP_CORE() is non-reentrant; calling this from inside a
// WiFi/lwIP event callback (which already runs on the TCPIP thread) would deadlock.
void CSIService::_restoreEthDefaultNetif() {
    if (_otaInProgress.load()) {
        Serial.println("[CSI] restoreEthDefault: OTA in progress, skip");
        return;
    }
    if (!ETH.linkUp()) {
        Serial.println("[CSI] restoreEthDefault: ETH link down, skip");
        return;
    }

    esp_netif_t* ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!ethNetif) {
        Serial.println("[CSI] restoreEthDefault: ETH_DEF netif not found");
        return;
    }

    esp_netif_ip_info_t ethIpInfo;
    if (esp_netif_get_ip_info(ethNetif, &ethIpInfo) != ESP_OK || ethIpInfo.ip.addr == 0) {
        Serial.println("[CSI] restoreEthDefault: ETH has no IP yet, skip");
        return;
    }

    struct netif* ethLwip = (struct netif*)esp_netif_get_netif_impl(ethNetif);
    if (!ethLwip) {
        Serial.println("[CSI] restoreEthDefault: failed to get ETH lwIP netif");
        return;
    }

    LOCK_TCPIP_CORE();
    netif_set_default(ethLwip);
    UNLOCK_TCPIP_CORE();

    Serial.println("[CSI] Set Ethernet as lwIP default netif (core-locked)");
}

void CSIService::begin(const char* ssid, const char* password,
                       MQTTService* mqtt, const char* topicPrefix,
                       Preferences* prefs) {
    _mqtt = mqtt;
    _prefs = prefs;
    strncpy(_topicPrefix, topicPrefix, sizeof(_topicPrefix) - 1);

    // Build MQTT topics
    snprintf(_tMotion,     sizeof(_tMotion),     "%s/motion",          _topicPrefix);
    snprintf(_tTurbulence, sizeof(_tTurbulence), "%s/turbulence",      _topicPrefix);
    snprintf(_tVariance,   sizeof(_tVariance),   "%s/variance",        _topicPrefix);
    snprintf(_tPhaseTurb,  sizeof(_tPhaseTurb),  "%s/phase_turbulence",_topicPrefix);
    snprintf(_tRatioTurb,  sizeof(_tRatioTurb),  "%s/ratio_turbulence",_topicPrefix);
    snprintf(_tBreathing,  sizeof(_tBreathing),  "%s/breathing_score", _topicPrefix);
    snprintf(_tComposite,  sizeof(_tComposite),  "%s/composite_score", _topicPrefix);
    snprintf(_tPackets,    sizeof(_tPackets),     "%s/packets",        _topicPrefix);
    snprintf(_tDser,       sizeof(_tDser),       "%s/dser",            _topicPrefix);
    snprintf(_tPlcr,       sizeof(_tPlcr),       "%s/plcr",            _topicPrefix);
    snprintf(_tMlProb,     sizeof(_tMlProb),     "%s/ml_probability",  _topicPrefix);
    snprintf(_tMlMotion,   sizeof(_tMlMotion),   "%s/ml_motion",       _topicPrefix);

    // Allocate turbulence buffer
    _turbBuffer = new (std::nothrow) float[_windowSize];
    if (!_turbBuffer) {
        Serial.println("[CSI] ERROR: Failed to allocate turbulence buffer");
        return;
    }
    memset(_turbBuffer, 0, _windowSize * sizeof(float));

    // Initialize low-pass filter coefficients (bilinear transform)
    float wc = tanf(M_PI * LOWPASS_CUTOFF_HZ / LOWPASS_SAMPLE_RATE_HZ);
    float k = 1.0f + wc;
    _lowpassState.b0 = wc / k;
    _lowpassState.a1 = (wc - 1.0f) / k;

    // Initialize WiFi with HT20/11n forcing
    _initWiFiForCSI(ssid, password);

    // Configure and enable CSI
    // csi10f: match espectre (csi_manager.cpp:289-295). Previous setup with
    // lltf_en=true + stbc_htltf2_en=true + ltf_merge_en=true was too permissive —
    // on WiFi 6 APs in backward-compat mode, L-LTF preamble entries fired the
    // callback at non-HT20 lengths which then dropped at the HT20_CSI_LEN gate
    // (net effect: packets=0, ht_ltf_seen=false even with healthy RSSI).
    // Disabling L-LTF / STBC sub-type / LTF-merge restricts the HW filter to
    // genuine HT-LTF only → clean 128 B callbacks on 11n-framed traffic from
    // the same APs previously diagnosed as "chipset-incompatible".
    wifi_csi_config_t csi_config = {};
    csi_config.lltf_en = false;
    csi_config.htltf_en = true;
    csi_config.stbc_htltf2_en = false;
    csi_config.ltf_merge_en = false;
    csi_config.channel_filter_en = false;

    esp_wifi_set_csi_config(&csi_config);
    esp_wifi_set_csi_rx_cb(_csiCallback, this);
    esp_wifi_set_csi(true);

    _active = true;
    Serial.printf("[CSI] CSI capture enabled (window=%d, threshold=%.2f)\n",
                  _windowSize, _threshold);

    _loadLearnedModel();

    // Start traffic generator for consistent CSI packet rate
    if (WiFi.status() == WL_CONNECTED) {
        _startTrafficGen();
    }
}

void CSIService::setWindowSize(uint16_t ws) {
    if (ws < 10) ws = 10;
    if (ws > 200) ws = 200;
    if (_turbBuffer) delete[] _turbBuffer;
    _windowSize = ws;
    _turbBuffer = new (std::nothrow) float[_windowSize];
    if (_turbBuffer) memset(_turbBuffer, 0, _windowSize * sizeof(float));
    _bufIndex = 0;
    _bufCount = 0;
    _runningMean = 0;
    _runningM2 = 0;
}

void CSIService::setTrafficRate(uint32_t pps) {
    if (pps < 10) pps = 10;
    if (pps > 500) pps = 500;
    _trafficRatePps = pps;
    // Restart traffic gen if running
    if (_trafficGenRunning.load()) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

void CSIService::setTrafficPort(uint16_t port) {
    if (port == 0) port = 7;
    _trafficPort = port;
    if (_trafficGenRunning.load() && !_trafficICMP) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

void CSIService::setTrafficICMP(bool icmp) {
    if (_trafficICMP == icmp) return;
    _trafficICMP = icmp;
    if (_trafficGenRunning.load()) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

// ============================================================================
// Traffic Generator (DNS UDP to gateway:53)
// ============================================================================

void CSIService::_startTrafficGen() {
    if (_trafficGenRunning.load()) return;

    esp_netif_t* esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!esp_netif) { DBG("CSI", "TrafficGen: no WiFi netif"); return; }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        DBG("CSI", "TrafficGen: WiFi has no IP — disabled");
        return;
    }

    _trafficGenRunning.store(true);

    BaseType_t result = xTaskCreate(
        _trafficGenTask, "csi_traffic", 4096, this, 5, &_trafficGenHandle);
    if (result != pdPASS) {
        DBG("CSI", "TrafficGen: task create failed");
        _trafficGenRunning.store(false);
        return;
    }

    DBG("CSI", "TrafficGen: %u pps via lwIP %s port=%u (WiFi)",
        _trafficRatePps, _trafficICMP ? "ICMP" : "UDP", _trafficPort);
}

void CSIService::_stopTrafficGen() {
    if (!_trafficGenRunning.load()) return;
    _trafficGenRunning.store(false);

    if (_trafficGenHandle) {
        for (int i = 0; i < 10 && eTaskGetState(_trafficGenHandle) != eDeleted; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        _trafficGenHandle = nullptr;
    }

    DBG("CSI", "TrafficGen stopped");
}

// ICMP echo reply discard callback (we don't need replies, just TX traffic for CSI)
static uint8_t _icmpRecvCb(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    pbuf_free(p);
    return 1; // consumed
}

void CSIService::_trafficGenTask(void* arg) {
    CSIService* svc = static_cast<CSIService*>(arg);
    if (!svc) { vTaskDelete(NULL); return; }

    // Get WiFi lwIP netif and target IP
    esp_netif_t* esp_nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (!esp_nif || esp_netif_get_ip_info(esp_nif, &ip_info) != ESP_OK) {
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    // Get raw lwIP netif from esp_netif — this is the key to interface-specific sending
    struct netif* wifi_netif = (struct netif*)esp_netif_get_netif_impl(esp_nif);
    if (!wifi_netif) {
        DBG("CSI", "TrafficGen: failed to get lwIP netif");
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    uint32_t target_ip = ip_info.gw.addr;
    if (target_ip == 0) {
        target_ip = (ip_info.ip.addr & ip_info.netmask.addr) | PP_HTONL(0x00000001UL);
    }

    ip_addr_t src_addr = {};
    src_addr.type = IPADDR_TYPE_V4;
    src_addr.u_addr.ip4.addr = ip_info.ip.addr;

    ip_addr_t dst_addr = {};
    dst_addr.type = IPADDR_TYPE_V4;
    dst_addr.u_addr.ip4.addr = target_ip;

    bool useICMP = svc->_trafficICMP;
    uint16_t port = svc->_trafficPort;

    // Create either UDP PCB or ICMP raw PCB
    struct udp_pcb* udp_pcb_ptr = nullptr;
    struct raw_pcb* raw_pcb_ptr = nullptr;

    LOCK_TCPIP_CORE();
    if (useICMP) {
        raw_pcb_ptr = raw_new(IP_PROTO_ICMP);
        if (raw_pcb_ptr) {
            raw_bind(raw_pcb_ptr, &src_addr);
            raw_recv(raw_pcb_ptr, _icmpRecvCb, nullptr);
            raw_bind_netif(raw_pcb_ptr, wifi_netif);
        }
    } else {
        udp_pcb_ptr = udp_new();
        if (udp_pcb_ptr) {
            udp_bind(udp_pcb_ptr, &src_addr, 0);
        }
    }
    UNLOCK_TCPIP_CORE();

    if (!udp_pcb_ptr && !raw_pcb_ptr) {
        DBG("CSI", "TrafficGen: PCB alloc failed (icmp=%d)", useICMP);
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    const uint32_t interval_us = 1000000 / svc->_trafficRatePps;
    int64_t next_send = esp_timer_get_time();
    uint16_t icmp_seq = 0;

    DBG("CSI", "TrafficGen task: netif=%c%c dst=" IPSTR " mode=%s port=%u",
                  wifi_netif->name[0], wifi_netif->name[1],
                  IP2STR((esp_ip4_addr_t*)&target_ip),
                  useICMP ? "ICMP" : "UDP", port);

    while (svc->_trafficGenRunning.load()) {
        err_t err = ERR_OK;

        if (useICMP) {
            // ICMP echo request (ping) — 8 byte header + 4 byte payload
            struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 4, PBUF_RAM);
            if (p) {
                struct icmp_echo_hdr* echo = (struct icmp_echo_hdr*)p->payload;
                ICMPH_TYPE_SET(echo, ICMP_ECHO);
                ICMPH_CODE_SET(echo, 0);
                echo->id = PP_HTONS(0xC510);
                echo->seqno = PP_HTONS(icmp_seq++);
                // 4 bytes payload
                memset((uint8_t*)p->payload + sizeof(struct icmp_echo_hdr), 0xAA, 4);
                echo->chksum = 0;
                echo->chksum = inet_chksum(p->payload, p->tot_len);

                LOCK_TCPIP_CORE();
                err = raw_sendto_if_src(raw_pcb_ptr, p, &dst_addr, wifi_netif, &src_addr);
                UNLOCK_TCPIP_CORE();
                pbuf_free(p);
            }
        } else {
            // UDP mode — send DNS query to configurable port
            struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, sizeof(DNS_QUERY), PBUF_RAM);
            if (p) {
                memcpy(p->payload, DNS_QUERY, sizeof(DNS_QUERY));
                LOCK_TCPIP_CORE();
                err = udp_sendto_if_src(udp_pcb_ptr, p, &dst_addr, port, wifi_netif, &src_addr);
                UNLOCK_TCPIP_CORE();
                pbuf_free(p);
            }
        }

        if (err != ERR_OK) {
            static uint32_t errCount = 0;
            if (errCount++ < 5) {
                DBG("CSI", "TrafficGen: send err=%d (icmp=%d)", err, useICMP);
            }
        }

        next_send += interval_us;
        int64_t now = esp_timer_get_time();
        int64_t sleep_us = next_send - now;

        if (sleep_us > 0) {
            vTaskDelay(pdMS_TO_TICKS((sleep_us + 999) / 1000));
        } else if (sleep_us < -100000) {
            next_send = now;
        }
    }

    LOCK_TCPIP_CORE();
    if (udp_pcb_ptr) udp_remove(udp_pcb_ptr);
    if (raw_pcb_ptr) raw_remove(raw_pcb_ptr);
    UNLOCK_TCPIP_CORE();

    vTaskDelete(NULL);
}

// ============================================================================
// CSI Packet Processing
// ============================================================================

void CSIService::_processCSI(wifi_csi_info_t* info) {
    const int8_t* buf = info->buf;
    int len = info->len;

    // --- Packet validation & normalization (ported from ESPectre) ---

    // STBC doubled packets: collapse 256→128 by averaging pairs
    int8_t collapsed[HT20_CSI_LEN];
    if (len == HT20_CSI_LEN_DOUBLE) {
        for (int i = 0; i < HT20_CSI_LEN; i++) {
            collapsed[i] = (int8_t)(((int)buf[i] + (int)buf[i + HT20_CSI_LEN]) / 2);
        }
        buf = collapsed;
        len = HT20_CSI_LEN;
    }

    // Short HT20 packets (57 SC = 114 bytes): remap with left guard padding
    int8_t remapped[HT20_CSI_LEN];
    if (len == HT20_CSI_LEN_SHORT) {
        memset(remapped, 0, HT20_CSI_LEN);
        memcpy(remapped + HT20_SHORT_LEFT_PAD, buf, HT20_CSI_LEN_SHORT);
        buf = remapped;
        len = HT20_CSI_LEN;
    }

    // Validate: only accept standard HT20 length
    if (len != HT20_CSI_LEN) return;

    // csi10c: first valid HT LTF frame → set AP-compatibility flag.
    // If AP emits only HE PHY (wifi6), this never flips true despite association.
    _htLtfSeen = true;

    int totalSc = len / 2; // 64

    // Extract amplitudes + phases for selected subcarriers
    float amps[NUM_SUBCARRIERS];
    float phases[NUM_SUBCARRIERS];
    uint8_t numAmps = 0;

    for (int i = 0; i < NUM_SUBCARRIERS; i++) {
        int sc = SUBCARRIERS[i];
        if (sc >= totalSc) continue;

        // Espressif CSI format: [Imaginary, Real] per subcarrier
        float Q = static_cast<float>(buf[sc * 2]);
        float I = static_cast<float>(buf[sc * 2 + 1]);
        amps[numAmps] = sqrtf(I * I + Q * Q);
        phases[numAmps] = atan2f(Q, I);
        numAmps++;
    }

    if (numAmps < 2) return;

    // --- DSER + PLCR (Uni-Fi features, arXiv 2601.10980) ---
    // amps[]/phases[] already in cache from extraction loop above.
    // numAmps == NUM_SUBCARRIERS in HT20 (all SUBCARRIERS indices < totalSc=64).
    {
        float dserSum = 0.0f;
        float dphaseSqSum = 0.0f;
        for (uint8_t i = 0; i < numAmps; i++) {
            _csiStatic[i] = DSER_STATIC_ALPHA * amps[i]
                          + (1.0f - DSER_STATIC_ALPHA) * _csiStatic[i];
            float Hd = amps[i] - _csiStatic[i];
            float Hs = _csiStatic[i];
            dserSum += logf((Hd * Hd + DSER_EPS) / (Hs * Hs + DSER_EPS));

            if (_hasPrevPhase) {
                float dp = phases[i] - _csiPhasePrev[i];
                while (dp >  (float)M_PI) dp -= 2.0f * (float)M_PI;
                while (dp < -(float)M_PI) dp += 2.0f * (float)M_PI;
                dphaseSqSum += dp * dp;
            }
            _csiPhasePrev[i] = phases[i];
        }
        _lastDser = dserSum / numAmps;
        _lastPlcr = _hasPrevPhase
                    ? sqrtf(dphaseSqSum / numAmps) / (2.0f * (float)M_PI)
                    : 0.0f;
        _hasPrevPhase = true;
    }

    // --- NBVI-lite per-subcarrier stability (csi5) ---
    // Update per-SC EWMA mean+variance every packet. Periodically rebuild
    // _nbviMask keeping K subcarriers with lowest CV (= most stable = best SNR).
    if (_nbviEnabled) {
        for (uint8_t i = 0; i < numAmps; i++) {
            float delta = amps[i] - _nbviMean[i];
            _nbviMean[i] += NBVI_ALPHA * delta;
            _nbviVar[i]   = (1.0f - NBVI_ALPHA) * (_nbviVar[i] + NBVI_ALPHA * delta * delta);
        }
        _nbviSamples++;

        if (_nbviSamples >= NBVI_MIN_SAMPLES &&
            (_nbviSamples - _nbviLastRecalcSamples) >= NBVI_RECALC_EVERY) {
            _nbviLastRecalcSamples = _nbviSamples;

            float best = 1e9f, worst = -1.0f;
            uint8_t bestIdx = 0, worstIdx = 0;
            float scores[12];
            for (uint8_t i = 0; i < numAmps; i++) {
                float m = _nbviMean[i];
                float s = sqrtf(_nbviVar[i] > 0 ? _nbviVar[i] : 0);
                float cv = (m > NBVI_EPS) ? (s / m) : 1e6f;
                _nbviScore[i] = cv;
                scores[i] = cv;
                if (cv < best)  { best = cv;  bestIdx = i; }
                if (cv > worst) { worst = cv; worstIdx = i; }
            }

            float sorted[12];
            memcpy(sorted, scores, sizeof(float) * numAmps);
            std::sort(sorted, sorted + numAmps);
            uint8_t k = (numAmps < NBVI_SELECT_K) ? numAmps : NBVI_SELECT_K;
            float cutoff = sorted[k - 1];
            uint8_t active = 0;
            for (uint8_t i = 0; i < numAmps; i++) {
                if (_nbviScore[i] <= cutoff && active < k) {
                    _nbviMask[i] = 1;
                    active++;
                } else {
                    _nbviMask[i] = 0;
                }
            }
            _nbviActiveCount = active;
            _nbviBestSC = SUBCARRIERS[bestIdx];
            _nbviWorstSC = SUBCARRIERS[worstIdx];
            _nbviBestScore = best;
            _nbviWorstScore = worst;
            _nbviRecalcCount++;
            _nbviReady = true;
        }
    }

    // --- Spatial turbulence (two-pass variance, CV normalized) ---
    // Breathing/DSER/PLCR downstream still use ampSum over all SCs (see _lastAmpSum).
    float ampSumAll = 0;
    for (uint8_t i = 0; i < numAmps; i++) ampSumAll += amps[i];

    // NBVI gate: when ready, restrict turbulence to most stable subcarriers.
    bool useNbvi = _nbviEnabled && _nbviReady && _nbviActiveCount >= 2;
    float ampSum = 0;
    uint8_t nUse = 0;
    if (useNbvi) {
        for (uint8_t i = 0; i < numAmps; i++) {
            if (_nbviMask[i]) { ampSum += amps[i]; nUse++; }
        }
        if (nUse < 2) { ampSum = ampSumAll; nUse = numAmps; useNbvi = false; }
    } else {
        ampSum = ampSumAll;
        nUse = numAmps;
    }
    float ampMean = ampSum / nUse;

    float ampVar = 0;
    for (uint8_t i = 0; i < numAmps; i++) {
        if (useNbvi && !_nbviMask[i]) continue;
        float d = amps[i] - ampMean;
        ampVar += d * d;
    }
    ampVar /= nUse;
    if (ampVar < 0) ampVar = 0;
    float rawStd = sqrtf(ampVar);

    // CV normalization: std/mean (gain-invariant for ESP32 without AGC lock)
    float turbulence = (ampMean > 0.0f) ? rawStd / ampMean : 0.0f;

    // --- Hampel outlier filter ---
    _hampelState.buffer[_hampelState.index] = turbulence;
    _hampelState.index = (_hampelState.index + 1) % HAMPEL_WINDOW;
    if (_hampelState.count < HAMPEL_WINDOW) _hampelState.count++;

    if (_hampelState.count >= 3) {
        float sorted[11];
        float deviations[11];
        uint8_t n = _hampelState.count;
        memcpy(sorted, _hampelState.buffer, n * sizeof(float));
        float median = calculateMedian(sorted, n);

        for (uint8_t i = 0; i < n; i++) {
            deviations[i] = fabsf(_hampelState.buffer[i] - median);
        }
        float mad = calculateMedian(deviations, n);

        float deviation = fabsf(turbulence - median);
        if (deviation > HAMPEL_THRESHOLD * MAD_SCALE * mad) {
            turbulence = median; // Replace outlier
        }
    }

    // --- Low-pass filter ---
    if (!_lowpassState.initialized) {
        _lowpassState.x_prev = turbulence;
        _lowpassState.y_prev = turbulence;
        _lowpassState.initialized = true;
    } else {
        float y = _lowpassState.b0 * turbulence + _lowpassState.b0 * _lowpassState.x_prev
                  - _lowpassState.a1 * _lowpassState.y_prev;
        _lowpassState.x_prev = turbulence;
        _lowpassState.y_prev = y;
        turbulence = y;
    }

    _lastTurbulence = turbulence;
    _lastAmpSum = ampSumAll;  // breathing filter + idle baseline see total energy (all 12 SCs)

    // --- Phase turbulence (std of inter-subcarrier phase diffs) ---
    if (numAmps > 2) {
        float pDiffs[NUM_SUBCARRIERS - 1];
        uint8_t nDiffs = 0;
        for (uint8_t i = 1; i < numAmps; i++) {
            pDiffs[nDiffs++] = phases[i] - phases[i - 1];
        }
        if (nDiffs > 1) {
            float pMean = 0;
            for (uint8_t i = 0; i < nDiffs; i++) pMean += pDiffs[i];
            pMean /= nDiffs;
            float pVar = 0;
            for (uint8_t i = 0; i < nDiffs; i++) {
                float d = pDiffs[i] - pMean;
                pVar += d * d;
            }
            pVar /= nDiffs;
            if (pVar < 0) pVar = 0;
            _lastPhaseTurb = sqrtf(pVar);
        }
    }

    // --- SA-WiSense ratio turbulence ---
    if (numAmps > 1) {
        float ratios[NUM_SUBCARRIERS - 1];
        uint8_t nRatios = 0;
        for (uint8_t i = 0; i + 1 < numAmps; i++) {
            if (amps[i + 1] > 0.1f) {
                ratios[nRatios++] = amps[i] / amps[i + 1];
            }
        }
        if (nRatios > 1) {
            float rMean = 0;
            for (uint8_t i = 0; i < nRatios; i++) rMean += ratios[i];
            rMean /= nRatios;
            float rVar = 0;
            for (uint8_t i = 0; i < nRatios; i++) {
                float d = ratios[i] - rMean;
                rVar += d * d;
            }
            rVar /= nRatios;
            if (rVar < 0) rVar = 0;
            _lastRatioTurb = sqrtf(rVar);
        }
    }

    // --- Breathing bandpass filter on amplitude sum ---
    if (!_breathFilter.initialized) {
        _breathFilter.hp_x_prev = ampSum;
        _breathFilter.initialized = true;
    } else {
        float hp = BREATH_HP_B0 * (ampSum - _breathFilter.hp_x_prev)
                    - BREATH_HP_A1 * _breathFilter.hp_y_prev;
        _breathFilter.hp_x_prev = ampSum;
        _breathFilter.hp_y_prev = hp;

        float lp = BREATH_LP_B0 * (hp + _breathFilter.lp_x_prev)
                    - BREATH_LP_A1 * _breathFilter.lp_y_prev;
        _breathFilter.lp_x_prev = hp;
        _breathFilter.lp_y_prev = lp;

        float sq = lp * lp;
        _breathFilter.energy = BREATH_ENERGY_ALPHA * sq
                               + (1.0f - BREATH_ENERGY_ALPHA) * _breathFilter.energy;
    }

    // --- Add to circular buffer (Welford's incremental variance) ---
    if (_turbBuffer) {
        float newVal = turbulence;
        if (_bufCount < _windowSize) {
            _turbBuffer[_bufIndex] = newVal;
            _bufCount++;
            float delta = newVal - _runningMean;
            _runningMean += delta / _bufCount;
            float delta2 = newVal - _runningMean;
            _runningM2 += delta * delta2;
        } else {
            float oldVal = _turbBuffer[_bufIndex];
            _turbBuffer[_bufIndex] = newVal;
            float newMean = _runningMean + (newVal - oldVal) / _windowSize;
            _runningM2 += (newVal - oldVal) * (newVal - newMean + oldVal - _runningMean);
            if (_runningM2 < 0) _runningM2 = 0;
            _runningMean = newMean;
        }
        _bufIndex = (_bufIndex + 1) % _windowSize;
        _runningVariance = (_bufCount >= _windowSize) ? _runningM2 / _windowSize : 0;
    }

    _totalPackets++;
    _windowPackets++;
}

// ============================================================================
// Motion State (temporal smoothing + hysteresis + breathing hold)
// ============================================================================

void CSIService::_updateMotionState() {
    if (_bufCount < _windowSize) return;

    // csi4: effective threshold is max of user-set and adaptive-P95.
    float effThr = _threshold;
    if (_adaptiveThresholdEnabled && _adaptiveThreshold > effThr) {
        effThr = _adaptiveThreshold;
    }

    bool rawMotion;
    if (!_motionState) {
        rawMotion = _runningVariance > effThr;
    } else {
        rawMotion = _runningVariance >= effThr * _hysteresis;
    }

    // N/M temporal smoothing (4/6 enter, 5/6 exit — matches ESPectre)
    _smoothHistory = ((_smoothHistory << 1) | (rawMotion ? 1 : 0)) & ((1 << SMOOTH_WINDOW) - 1);
    if (_smoothCount < SMOOTH_WINDOW) _smoothCount++;

    uint8_t motionCount = 0;
    uint8_t h = _smoothHistory;
    for (uint8_t i = 0; i < _smoothCount; i++) {
        motionCount += (h & 1);
        h >>= 1;
    }

    bool detectorMotion;
    if (!_motionState) {
        detectorMotion = (motionCount >= SMOOTH_ENTER && _smoothCount >= SMOOTH_ENTER);
    } else {
        uint8_t idleCount = _smoothCount - motionCount;
        detectorMotion = !(idleCount >= SMOOTH_EXIT && _smoothCount >= SMOOTH_EXIT);
    }

    // Breathing-aware presence hold (ported from ESPectre)
    // If detector says IDLE but breathing/phase suggest stationary person → hold MOTION
    if (!detectorMotion && _motionState && _idleInitialized) {
        float breathScore = getBreathingScore();
        bool breathHold = (breathScore > _idleMeanTurb * 2.0f) &&
                          (_lastPhaseTurb > _idleMeanPhase * 1.5f);

        if (breathHold && _breathHoldCount < BREATH_HOLD_MAX) {
            _breathHoldCount++;
            return; // Keep MOTION state, don't update
        }
    }

    if (detectorMotion) {
        _motionState = true;
        _breathHoldCount = 0;
    } else {
        _motionState = false;
        _breathHoldCount = 0;
    }

    // Update idle baselines when idle (EMA)
    if (!_motionState && _bufCount >= _windowSize) {
        float alpha = 1.0f / _windowSize;
        if (!_idleInitialized) {
            _idleMeanTurb = _lastTurbulence;
            _idleMeanPhase = _lastPhaseTurb;
            _idleAmpBaseline = _lastAmpSum;  // Real amplitude sum, not placeholder
            _idleInitialized = true;
        } else {
            _idleMeanTurb = alpha * _lastTurbulence + (1.0f - alpha) * _idleMeanTurb;
            _idleMeanPhase = alpha * _lastPhaseTurb + (1.0f - alpha) * _idleMeanPhase;
            _idleAmpBaseline = alpha * _lastAmpSum + (1.0f - alpha) * _idleAmpBaseline;
        }
    }
}

// ============================================================================
// csi10: Continuous EMA refresh of learned site model
// ----------------------------------------------------------------------------
// After one-shot site-learning completes, keep nudging _learnedThreshold /
// _learnedMeanVar / _learnedStdVar toward current quiet-site samples so the
// model adapts to seasonal / furniture / HVAC changes without a re-train.
//
// Guards (all must hold, else tick is ignored):
//   - learned model exists (_siteModelReady, sample count > 0)
//   - NOT currently in one-shot site-learning mode
//   - radar ground-truth says IDLE (_radarPresent == false)
//   - CSI variance path also says IDLE (_motionState == false)
//   - MLP path (if enabled) also says IDLE
//   - current variance < 0.7 * learned_threshold (clean sample, not outlier)
//
// EMA alpha = 1/3600 at 1 Hz publish tick ≈ 1 h time constant. Safety floor:
// learned_threshold is clamped to user-set _threshold (GUI sensitivity slider)
// so it can never drop below what the user explicitly configured.
// NVS save cadence: every 10 min (144 writes/day / entry ≈ 2-year flash life).
// ============================================================================

void CSIService::_continuousLearnRefresh() {
    if (!_siteModelReady || _learnedSampleCount == 0) return;
    if (_siteLearningActive) return;
    if (_radarPresent) return;
    if (_motionState) return;
    if (_mlEnabled && _mlMotion) return;
    if (_bufCount < _windowSize) return;

    float v = _runningVariance;
    if (v <= 0.0f) return;
    if (v > _learnedThreshold * 0.7f) return;  // only clean idle samples

    constexpr float alpha = 1.0f / 3600.0f;
    _learnedMeanVar = alpha * v + (1.0f - alpha) * _learnedMeanVar;
    float dev = std::fabs(v - _learnedMeanVar);
    _learnedStdVar = alpha * dev + (1.0f - alpha) * _learnedStdVar;

    float newThr = _learnedMeanVar + 3.0f * _learnedStdVar;
    // csi10e: hard absolute floor before any other clamp
    if (newThr < MIN_LEARNED_THRESHOLD) newThr = MIN_LEARNED_THRESHOLD;
    if (newThr < _threshold) newThr = _threshold;  // never drop below user-set sensitivity
    _learnedThreshold = newThr;

    _learnRefreshCount++;

    unsigned long now = millis();
    if (now - _lastLearnRefreshSaveMs > 600000UL) {  // 10 min
        _saveLearnedModel(_learnedThreshold, _learnedMeanVar, _learnedStdVar,
                          _learnedMaxVar, _learnedSampleCount);
        _lastLearnRefreshSaveMs = now;
    }
}

// ============================================================================
// csi8: MLP motion inference (15 -> 16 -> 8 -> 1) — parallel A/B path
// ============================================================================

void CSIService::_runMlInference() {
    if (!_mlEnabled || _bufCount < _windowSize) {
        _mlProbability = 0.0f;
        return;
    }

    // Feature extraction from turbulence buffer + multi-domain signals.
    // Buffer is circular; copy into contiguous tmp in insertion order so
    // slope/autocorr/zcr see the true temporal ordering.
    float turb[128];
    uint16_t n = _bufCount < _windowSize ? _bufCount : _windowSize;
    if (n > 128) n = 128;
    uint16_t start = (_bufIndex + _windowSize - n) % _windowSize;
    for (uint16_t i = 0; i < n; i++) {
        turb[i] = _turbBuffer[(start + i) % _windowSize];
    }

    float feats[csi_ml::ML_NUM_FEATURES];
    csi_ml::extract_ml_features(turb, n,
                                _lastPhaseTurb, _lastRatioTurb, getBreathingScore(),
                                _lastDser, _lastPlcr,
                                feats);

    // StandardScaler + MLP forward pass (17 -> 18 -> 9 -> 1)
    float norm[csi_ml::ML_NUM_FEATURES];
    for (uint8_t i = 0; i < csi_ml::ML_NUM_FEATURES; i++) {
        float s = csi_ml::ML_FEATURE_SCALE[i];
        norm[i] = (feats[i] - csi_ml::ML_FEATURE_MEAN[i]) / (s > 1e-10f ? s : 1e-10f);
    }

    float h1[csi_ml::ML_H1];
    for (int j = 0; j < csi_ml::ML_H1; j++) {
        float a = csi_ml::ML_B1[j];
        for (int i = 0; i < csi_ml::ML_NUM_FEATURES; i++) a += norm[i] * csi_ml::ML_W1[i][j];
        h1[j] = a > 0.0f ? a : 0.0f;
    }
    float h2[csi_ml::ML_H2];
    for (int j = 0; j < csi_ml::ML_H2; j++) {
        float a = csi_ml::ML_B2[j];
        for (int i = 0; i < csi_ml::ML_H1; i++) a += h1[i] * csi_ml::ML_W2[i][j];
        h2[j] = a > 0.0f ? a : 0.0f;
    }
    float out = csi_ml::ML_B3[0];
    for (int i = 0; i < csi_ml::ML_H2; i++) out += h2[i] * csi_ml::ML_W3[i][0];

    if (out < -20.0f) _mlProbability = 0.0f;
    else if (out > 20.0f) _mlProbability = 1.0f;
    else _mlProbability = 1.0f / (1.0f + std::exp(-out));

    // Dual-threshold hysteresis then N/M smoothing (reuses SMOOTH_WINDOW from header).
    bool raw = _mlMotion ? (_mlProbability >= _mlThreshold * ML_EXIT_FACTOR)
                         : (_mlProbability >  _mlThreshold);
    _mlSmoothHistory = ((_mlSmoothHistory << 1) | (raw ? 1 : 0)) & ((1 << SMOOTH_WINDOW) - 1);
    if (_mlSmoothCount < SMOOTH_WINDOW) _mlSmoothCount++;

    uint8_t mc = 0;
    uint8_t h = _mlSmoothHistory;
    for (uint8_t i = 0; i < _mlSmoothCount; i++) { mc += (h & 1); h >>= 1; }
    if (!_mlMotion) {
        _mlMotion = (mc >= SMOOTH_ENTER && _mlSmoothCount >= SMOOTH_ENTER);
    } else {
        uint8_t ic = _mlSmoothCount - mc;
        _mlMotion = !(ic >= SMOOTH_EXIT && _mlSmoothCount >= SMOOTH_EXIT);
    }
}

// ============================================================================
// Accessors
// ============================================================================

float CSIService::getBreathingScore() const {
    return _breathFilter.initialized ? sqrtf(_breathFilter.energy) : 0.0f;
}

float CSIService::getCompositeScore() const {
    if (!_idleInitialized) return 0.0f;

    float turbDev = 0;
    if (_idleMeanTurb > 1e-6f) {
        turbDev = (_runningMean - _idleMeanTurb) / _idleMeanTurb;
        if (turbDev < 0) turbDev = 0;
    }
    float phaseDev = 0;
    if (_idleMeanPhase > 1e-6f) {
        phaseDev = (_lastPhaseTurb - _idleMeanPhase) / _idleMeanPhase;
        if (phaseDev < 0) phaseDev = 0;
    }

    return 0.35f * turbDev + 0.25f * phaseDev +
           0.20f * _lastRatioTurb + 0.20f * getBreathingScore();
}

// ============================================================================
// MQTT Publishing
// ============================================================================

void CSIService::_publishMQTT() {
    if (!_mqtt || !_mqtt->connected()) return;

    char val[16];

    snprintf(val, sizeof(val), "%s", _motionState ? "ON" : "OFF");
    _mqtt->publish(_tMotion, val, true);

    snprintf(val, sizeof(val), "%.4f", _lastTurbulence);
    _mqtt->publish(_tTurbulence, val);

    snprintf(val, sizeof(val), "%.4f", _runningVariance);
    _mqtt->publish(_tVariance, val);

    snprintf(val, sizeof(val), "%.4f", _lastPhaseTurb);
    _mqtt->publish(_tPhaseTurb, val);

    snprintf(val, sizeof(val), "%.4f", _lastRatioTurb);
    _mqtt->publish(_tRatioTurb, val);

    snprintf(val, sizeof(val), "%.4f", getBreathingScore());
    _mqtt->publish(_tBreathing, val);

    snprintf(val, sizeof(val), "%.4f", getCompositeScore());
    _mqtt->publish(_tComposite, val);

    snprintf(val, sizeof(val), "%lu", (unsigned long)_totalPackets);
    _mqtt->publish(_tPackets, val);

    snprintf(val, sizeof(val), "%.4f", _lastDser);
    _mqtt->publish(_tDser, val);

    snprintf(val, sizeof(val), "%.4f", _lastPlcr);
    _mqtt->publish(_tPlcr, val);

    if (_mlEnabled) {
        snprintf(val, sizeof(val), "%.4f", _mlProbability);
        _mqtt->publish(_tMlProb, val);
        snprintf(val, sizeof(val), "%s", _mlMotion ? "ON" : "OFF");
        _mqtt->publish(_tMlMotion, val, true);
    }
}

// ============================================================================
// Main loop update
// ============================================================================

void CSIService::update() {
    if (!_active) return;

    // csi7b: Pause all WiFi/lwIP manipulation while an OTA transfer is active.
    // WiFi.reconnect(), netif_set_default() and raw_sendto traffic gen all risk
    // dropping the in-flight OTA TCP stream. Hooks in main.cpp / WebRoutes.cpp
    // set this flag at OTA start and clear it at end/error.
    if (_otaInProgress.load()) return;

    // Force reconnect requested by user
    if (_reconnectRequested) {
        _reconnectRequested = false;
        _stopTrafficGen();
        WiFi.reconnect();
        Serial.println("[CSI] Forced WiFi reconnect");
    }

    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        // Stop traffic gen when WiFi is down
        if (_trafficGenRunning.load()) _stopTrafficGen();

        static uint32_t lastReconnect = 0;
        if (millis() - lastReconnect > 10000) {
            WiFi.reconnect();
            lastReconnect = millis();
        }
        return;
    }

    // Restart traffic gen after WiFi reconnect
    if (!_trafficGenRunning.load() && WiFi.status() == WL_CONNECTED) {
        static uint32_t lastAttempt = 0;
        if (millis() - lastAttempt > 5000) {
            lastAttempt = millis();
            DBG("CSI", "TrafficGen retry: WiFi=%d IP=%s",
                WiFi.status(), WiFi.localIP().toString().c_str());
            // csi7: each WiFi (re)connect resets lwIP default netif to WiFi.
            // Restore Ethernet as default before starting traffic gen.
            _restoreEthDefaultNetif();
            _startTrafficGen();
        }
    }

    // Periodic CSI diagnostics (every 30s)
    static uint32_t lastDiag = 0;
    if (millis() - lastDiag > 30000) {
        lastDiag = millis();
        int rssi = WiFi.RSSI();
        DBG("CSI", "diag: pps=%.1f pkts=%lu tgen=%d wifi=%d ip=%s rssi=%d",
            _packetRate, (unsigned long)_totalPackets,
            _trafficGenRunning.load() ? 1 : 0,
            WiFi.status(), WiFi.localIP().toString().c_str(), rssi);
        if (rssi > -40) {
            DBG("CSI", "RSSI WARN: %d dBm — too strong, may saturate near-AP", rssi);
        } else if (rssi < -70) {
            DBG("CSI", "RSSI WARN: %d dBm — too weak, low SNR", rssi);
        }
    }

    uint32_t now = millis();
    if (now - _lastPublishMs >= _publishIntervalMs) {
        uint32_t windowMs = now - _lastPublishMs;
        if (windowMs > 0) _packetRate = (float)_windowPackets * 1000.0f / (float)windowMs;
        _lastPublishMs = now;
        _updateMotionState();
        _runMlInference();
        _continuousLearnRefresh();

        // Calibration sample collection
        if (_calibrating && _bufCount >= _windowSize) {
            _calibVarSum += _runningVariance;
            _calibSamples++;
            if (now - _calibStartMs >= _calibDurationMs) {
                if (_calibSamples > 0) {
                    float mean = _calibVarSum / _calibSamples;
                    float newThr = mean * 1.5f;
                    if (newThr < 0.001f) newThr = 0.001f;
                    _threshold = newThr;
                    _baseThreshold = newThr;          // csi2: sync base so stuck-raise is relative to calibrated value
                    _stuckRaiseCount = 0;
                    _stuckMotionCount = 0;
                    if (_prefs) _prefs->putFloat("csi_thr", _threshold);
                    Serial.printf("[CSI] Calibration done: %u samples, mean=%.4f, threshold=%.4f\n",
                                  _calibSamples, mean, _threshold);
                }
                _calibrating = false;
            }
        }

        // Auto-calibration on quiet environment (port from espectre 503ec04).
        // Once per boot: when running_mean < 25% thr and running_variance < 5% thr^2
        // hold for N minutes → trigger calibrateThreshold(10s). Resets on any sample
        // breaching the bounds. Guaranteed clean quiet baseline without manual action.
        if (_autoCalEnabled && !_autoCalDone && !_calibrating && _bufCount >= _windowSize) {
            float thr = _threshold;
            bool quiet = (_runningMean < thr * 0.25f) &&
                         (_runningVariance < thr * thr * 0.05f);
            if (quiet) {
                if (_autoCalQuietStart == 0) {
                    _autoCalQuietStart = now;
                    Serial.printf("[CSI] Auto-cal: quiet env detected, waiting %us...\n",
                                  _autoCalQuietSeconds);
                } else if ((now - _autoCalQuietStart) / 1000UL >= _autoCalQuietSeconds) {
                    Serial.printf("[CSI] Auto-cal: quiet for %us — triggering recalibration\n",
                                  _autoCalQuietSeconds);
                    _autoCalDone = true;
                    calibrateThreshold(10000);
                }
            } else if (_autoCalQuietStart != 0) {
                _autoCalQuietStart = 0;
            }
        }

        // csi3: detect AP roam (BSSID change) → invalidate baseline + rerun auto-cal.
        // Channel/AP change makes existing stats stale; cheapest fix is full reset.
        if (WiFi.status() == WL_CONNECTED) {
            uint8_t* curBSSID = WiFi.BSSID();
            if (curBSSID != nullptr) {
                if (!_bssidInitialized) {
                    memcpy(_lastBSSID, curBSSID, 6);
                    _bssidInitialized = true;
                } else if (memcmp(_lastBSSID, curBSSID, 6) != 0) {
                    Serial.printf("[CSI] BSSID change: %02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X — resetting baseline\n",
                        _lastBSSID[0], _lastBSSID[1], _lastBSSID[2], _lastBSSID[3], _lastBSSID[4], _lastBSSID[5],
                        curBSSID[0], curBSSID[1], curBSSID[2], curBSSID[3], curBSSID[4], curBSSID[5]);
                    memcpy(_lastBSSID, curBSSID, 6);
                    _bssidChangeCount++;
                    resetIdleBaseline();
                    resetAutoCalibration();
                    // Also drop adaptive-P95 buffer: old noise stats no longer valid.
                    _p95BufIndex = 0;
                    _p95BufCount = 0;
                    _adaptiveThreshold = 0.0f;
                    _p95TickSinceUpdate = 0;
                    // csi5: re-learn per-SC stability on new AP.
                    _nbviReady = false;
                    _nbviSamples = 0;
                    _nbviLastRecalcSamples = 0;
                    _nbviActiveCount = NUM_SUBCARRIERS;
                    for (uint8_t i = 0; i < NUM_SUBCARRIERS; i++) {
                        _nbviMean[i] = 0; _nbviVar[i] = 0; _nbviScore[i] = 0; _nbviMask[i] = 1;
                    }
                    // csi6b: if site-learning is in progress, discard accumulated stats
                    // and restart the window on the new AP — mixing two APs' variance
                    // distributions gives a meaningless mean/σ.
                    if (_siteLearningActive) {
                        Serial.printf("[CSI] Site learn reset on BSSID change: %lu samples discarded\n",
                                      (unsigned long)_siteLearnAccepted);
                        _siteLearnAccepted = 0;
                        _siteLearnRejectedMotion = 0;
                        _siteLearnRejectedRadar = 0;
                        _siteLearnMeanVarAcc = 0.0f;
                        _siteLearnM2Var = 0.0f;
                        _siteLearnMaxVar = 0.0f;
                        _siteLearnStartMs = millis();
                        _siteLearnBssidResetCount++;
                    }
                }
            }
        }

        // csi2: stuck-in-motion escalation. Count consecutive MOTION publish ticks;
        // after STUCK_MOTION_LIMIT (~24h @ 1Hz) raise threshold ×1.5 and reset counter.
        // Max STUCK_RAISE_MAX raises per boot. Any IDLE transition resets the counter
        // so transient motion doesn't accumulate.
        if (_motionState) {
            _stuckMotionCount++;
            if (_stuckMotionCount >= STUCK_MOTION_LIMIT && _stuckRaiseCount < STUCK_RAISE_MAX) {
                float newThr = _threshold * STUCK_RAISE_FACTOR;
                if (newThr > 100.0f) newThr = 100.0f;
                Serial.printf("[CSI] Stuck-motion raise %u/%u: threshold %.4f -> %.4f\n",
                              _stuckRaiseCount + 1, STUCK_RAISE_MAX, _threshold, newThr);
                _threshold = newThr;
                _stuckRaiseCount++;
                _stuckMotionCount = 0;
                // Flush running stats so raised threshold gets clean input
                _bufCount = 0;
                _bufIndex = 0;
                _runningMean = 0;
                _runningM2 = 0;
                _runningVariance = 0;
            }
        } else {
            _stuckMotionCount = 0;
        }

        // csi4: adaptive P95 rolling threshold. Append variance sample to ring buffer;
        // every P95_UPDATE_EVERY ticks recompute P95 × P95_FACTOR. Effective threshold
        // (used by _updateMotionState) = max(_threshold, _adaptiveThreshold).
        if (_bufCount >= _windowSize) {
            _p95Buffer[_p95BufIndex] = _runningVariance;
            _p95BufIndex = (_p95BufIndex + 1) % P95_BUFFER_SIZE;
            if (_p95BufCount < P95_BUFFER_SIZE) _p95BufCount++;
            _p95TickSinceUpdate++;
            if (_p95TickSinceUpdate >= P95_UPDATE_EVERY && _p95BufCount >= 30) {
                _p95TickSinceUpdate = 0;
                // Copy circular buffer to scratch (O(n)) then nth_element (O(n) avg)
                static float scratch[P95_BUFFER_SIZE];
                uint16_t n = _p95BufCount;
                for (uint16_t i = 0; i < n; i++) scratch[i] = _p95Buffer[i];
                uint16_t idx95 = (uint16_t)((n - 1) * 0.95f);
                std::nth_element(scratch, scratch + idx95, scratch + n);
                _adaptiveThreshold = scratch[idx95] * P95_FACTOR;
            }
        }

        // csi6/csi6b: long-term quiet-site learning. Welford mean+M2 on _runningVariance,
        // samples rejected whenever CSI flags motion OR LD2412 radar sees presence
        // (stationary human with low CSI variance would otherwise poison baseline).
        if (_siteLearningActive && _bufCount >= _windowSize) {
            if (_motionState) {
                _siteLearnRejectedMotion++;
            } else if (_radarPresent) {
                _siteLearnRejectedRadar++;
            } else {
                _siteLearnAccepted++;
                float delta = _runningVariance - _siteLearnMeanVarAcc;
                _siteLearnMeanVarAcc += delta / (float)_siteLearnAccepted;
                float delta2 = _runningVariance - _siteLearnMeanVarAcc;
                _siteLearnM2Var += delta * delta2;
                if (_runningVariance > _siteLearnMaxVar) {
                    _siteLearnMaxVar = _runningVariance;
                }
            }
            if (now - _siteLearnStartMs >= _siteLearnDurationMs) {
                _finalizeSiteLearning();
            }
        }

        _publishMQTT();
        _windowPackets = 0;
    }
}

// ============================================================================
// Diagnostics / control
// ============================================================================

int CSIService::getWifiRSSI() const {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

String CSIService::getWifiSSID() const {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
}

void CSIService::resetIdleBaseline() {
    _idleInitialized = false;
    _idleMeanTurb = 0;
    _idleMeanPhase = 0;
    _idleAmpBaseline = 0;
    _bufCount = 0;
    _bufIndex = 0;
    _runningMean = 0;
    _runningM2 = 0;
    _runningVariance = 0;
    _smoothHistory = 0;
    _smoothCount = 0;
    _motionState = false;
    _breathHoldCount = 0;
    _hampelState.index = 0;
    _hampelState.count = 0;
    _lowpassState.initialized = false;
    if (_turbBuffer) memset(_turbBuffer, 0, _windowSize * sizeof(float));
    memset(_csiStatic,    0, sizeof(_csiStatic));
    memset(_csiPhasePrev, 0, sizeof(_csiPhasePrev));
    _hasPrevPhase = false;
    _lastDser = 0.0f;
    _lastPlcr = 0.0f;
    Serial.println("[CSI] Idle baseline reset — recollecting samples");
}

void CSIService::forceReconnect() {
    _reconnectRequested = true;
}

void CSIService::calibrateThreshold(uint32_t durationMs) {
    if (durationMs < 1000) durationMs = 1000;
    if (durationMs > 60000) durationMs = 60000;
    _calibrating = true;
    _calibStartMs = millis();
    _calibDurationMs = durationMs;
    _calibVarSum = 0.0f;
    _calibSamples = 0;
    Serial.printf("[CSI] Calibration started — sampling %u ms (keep area still)\n", durationMs);
}

float CSIService::getCalibrationProgress() const {
    if (!_calibrating || _calibDurationMs == 0) return 0.0f;
    uint32_t elapsed = millis() - _calibStartMs;
    if (elapsed >= _calibDurationMs) return 1.0f;
    return (float)elapsed / (float)_calibDurationMs;
}

float CSIService::getEffectiveThreshold() const {
    float eff = _threshold;
    if (_adaptiveThresholdEnabled && _adaptiveThreshold > eff) {
        eff = _adaptiveThreshold;
    }
    return eff;
}

uint16_t CSIService::getNbviMask() const {
    uint16_t m = 0;
    for (uint8_t i = 0; i < NUM_SUBCARRIERS; i++) {
        if (_nbviMask[i]) m |= (uint16_t)(1u << i);
    }
    return m;
}

// ============================================================================
// csi6: Long-term quiet-site learning
// ============================================================================

bool CSIService::startSiteLearning(uint32_t durationMs) {
    if (durationMs < 60000) durationMs = 60000;
    if (durationMs > 604800000UL) durationMs = 604800000UL; // 7 days

    resetIdleBaseline();

    _siteLearningActive = true;
    _siteLearnStartMs = millis();
    _siteLearnDurationMs = durationMs;
    _siteLearnAccepted = 0;
    _siteLearnRejectedMotion = 0;
    _siteLearnMeanVarAcc = 0.0f;
    _siteLearnM2Var = 0.0f;
    _siteLearnMaxVar = 0.0f;

    Serial.printf("[CSI] Site learning started — duration=%lu s, keep site empty\n",
                  (unsigned long)(durationMs / 1000));
    return true;
}

void CSIService::stopSiteLearning() {
    if (!_siteLearningActive) return;
    _siteLearningActive = false;
    Serial.println("[CSI] Site learning stopped");
}

void CSIService::clearLearnedSiteModel() {
    stopSiteLearning();

    _siteModelReady = false;
    _learnedThreshold = 0.0f;
    _learnedMeanVar = 0.0f;
    _learnedStdVar = 0.0f;
    _learnedMaxVar = 0.0f;
    _learnedSampleCount = 0;
    _learnedIdleMeanTurb = 0.0f;
    _learnedIdleMeanPhase = 0.0f;
    _learnedIdleAmpBaseline = 0.0f;

    if (_prefs) {
        _prefs->remove("csi_lrn_ok");
        _prefs->remove("csi_lrn_thr");
        _prefs->remove("csi_lrn_mu");
        _prefs->remove("csi_lrn_std");
        _prefs->remove("csi_lrn_max");
        _prefs->remove("csi_lrn_n");
        _prefs->remove("csi_lrn_turb");
        _prefs->remove("csi_lrn_ph");
        _prefs->remove("csi_lrn_amp");
    }

    Serial.println("[CSI] Learned site model cleared");
}

float CSIService::getSiteLearningProgress() const {
    if (!_siteLearningActive || _siteLearnDurationMs == 0) return 0.0f;
    uint32_t elapsed = millis() - _siteLearnStartMs;
    if (elapsed >= _siteLearnDurationMs) return 1.0f;
    return (float)elapsed / (float)_siteLearnDurationMs;
}

uint32_t CSIService::getSiteLearningElapsedSec() const {
    if (!_siteLearningActive) return 0;
    return (millis() - _siteLearnStartMs) / 1000;
}

float CSIService::getSiteLearningThresholdEstimate() const {
    if (_siteLearnAccepted == 0) return _threshold;
    return _computeSiteLearningThreshold();
}

void CSIService::_loadLearnedModel() {
    if (!_prefs || !_prefs->getBool("csi_lrn_ok", false)) return;

    _siteModelReady = true;
    _learnedThreshold = _prefs->getFloat("csi_lrn_thr", _threshold);
    _learnedMeanVar = _prefs->getFloat("csi_lrn_mu", 0.0f);
    _learnedStdVar = _prefs->getFloat("csi_lrn_std", 0.0f);
    _learnedMaxVar = _prefs->getFloat("csi_lrn_max", 0.0f);
    _learnedSampleCount = _prefs->getUInt("csi_lrn_n", 0);
    _learnedIdleMeanTurb = _prefs->getFloat("csi_lrn_turb", 0.0f);
    _learnedIdleMeanPhase = _prefs->getFloat("csi_lrn_ph", 0.0f);
    _learnedIdleAmpBaseline = _prefs->getFloat("csi_lrn_amp", 0.0f);

    if (_learnedIdleMeanTurb > 0.0f || _learnedIdleMeanPhase > 0.0f) {
        _idleMeanTurb = _learnedIdleMeanTurb;
        _idleMeanPhase = _learnedIdleMeanPhase;
        _idleAmpBaseline = _learnedIdleAmpBaseline;
        _idleInitialized = true;
    }

    Serial.printf("[CSI] Loaded learned site model: thr=%.4f samples=%lu\n",
                  _learnedThreshold, (unsigned long)_learnedSampleCount);
}

void CSIService::_saveLearnedModel(float threshold, float meanVar, float stdVar, float maxVar, uint32_t samples) {
    if (!_prefs) return;

    _prefs->putBool("csi_lrn_ok", true);
    _prefs->putFloat("csi_lrn_thr", threshold);
    _prefs->putFloat("csi_lrn_mu", meanVar);
    _prefs->putFloat("csi_lrn_std", stdVar);
    _prefs->putFloat("csi_lrn_max", maxVar);
    _prefs->putUInt("csi_lrn_n", samples);
    _prefs->putFloat("csi_lrn_turb", _idleMeanTurb);
    _prefs->putFloat("csi_lrn_ph", _idleMeanPhase);
    _prefs->putFloat("csi_lrn_amp", _idleAmpBaseline);

    // Keep existing config path in sync so threshold survives reboot and UI edits.
    _prefs->putFloat("csi_thr", threshold);
}

float CSIService::_computeSiteLearningThreshold() const {
    if (_siteLearnAccepted == 0) return _threshold;

    float variance = (_siteLearnAccepted > 1)
        ? (_siteLearnM2Var / (float)(_siteLearnAccepted - 1))
        : 0.0f;
    float stddev = sqrtf(variance);
    float candidate = _siteLearnMeanVarAcc + 6.0f * stddev;
    float maxGuard = _siteLearnMaxVar * 1.15f;

    if (candidate < maxGuard) candidate = maxGuard;
    // csi10e: absolute floor raised 0.001 → 0.005; lower produced hair-trigger
    // baselines on very quiet production sites (see 2026-04-24 false-alarm report).
    if (candidate < MIN_LEARNED_THRESHOLD) candidate = MIN_LEARNED_THRESHOLD;
    if (candidate > 100.0f) candidate = 100.0f;
    return candidate;
}

void CSIService::_finalizeSiteLearning() {
    _siteLearningActive = false;

    if (_siteLearnAccepted < 30) {
        Serial.printf("[CSI] Site learning aborted: only %lu quiet samples\n",
                      (unsigned long)_siteLearnAccepted);
        return;
    }

    float threshold = _computeSiteLearningThreshold();
    float variance = (_siteLearnAccepted > 1)
        ? (_siteLearnM2Var / (float)(_siteLearnAccepted - 1))
        : 0.0f;
    float stddev = sqrtf(variance);

    _threshold = threshold;
    _siteModelReady = true;
    _learnedThreshold = threshold;
    _learnedMeanVar = _siteLearnMeanVarAcc;
    _learnedStdVar = stddev;
    _learnedMaxVar = _siteLearnMaxVar;
    _learnedSampleCount = _siteLearnAccepted;
    _learnedIdleMeanTurb = _idleMeanTurb;
    _learnedIdleMeanPhase = _idleMeanPhase;
    _learnedIdleAmpBaseline = _idleAmpBaseline;

    _saveLearnedModel(threshold, _learnedMeanVar, _learnedStdVar, _learnedMaxVar, _learnedSampleCount);

    Serial.printf(
        "[CSI] Site learning done: quiet=%lu rejected=%lu mean=%.6f std=%.6f max=%.6f threshold=%.6f\n",
        (unsigned long)_siteLearnAccepted,
        (unsigned long)_siteLearnRejectedMotion,
        _learnedMeanVar,
        _learnedStdVar,
        _learnedMaxVar,
        _learnedThreshold
    );
}
