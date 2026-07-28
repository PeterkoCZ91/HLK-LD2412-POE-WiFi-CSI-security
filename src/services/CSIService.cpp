#include "services/CSIService.h"
#include "services/MQTTService.h"
#include "services/ml_features.h"
#include "services/ml_weights.h"
#include "debug.h"
#include <ETH.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <ctime>
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
void CSIService::_csiCallback(void* ctx, wifi_csi_info_t* info) {
    // Běží na vysokoprioritním WiFi tasku (ppTask, core 0). Jen memcpy do
    // fronty — plná pipeline tady vyhladověla IDLE0 a shodila task WDT
    // (coredump, rc4 éra). Zpracování dělá csi_proc na core 1.
    if (!ctx || !info || !info->buf || info->len == 0) return;
    CSIService* self = static_cast<CSIService*>(ctx);
    if (self->_csiFrameQueue == nullptr) return;
    CsiFrame f;
    f.len = (info->len > sizeof(f.buf)) ? (uint16_t)sizeof(f.buf) : (uint16_t)info->len;
    memcpy(f.buf, info->buf, f.len);
    if (xQueueSend(self->_csiFrameQueue, &f, 0) != pdTRUE) {
        self->_csiQueueDrops.fetch_add(1, std::memory_order_relaxed);
    }
}

void CSIService::_csiProcTask(void* arg) {
    CSIService* self = static_cast<CSIService*>(arg);
    CsiFrame f;
    for (;;) {
        self->_processPendingModelOperation();
        if (xQueueReceive(self->_csiFrameQueue, &f, pdMS_TO_TICKS(20)) == pdTRUE) {
            self->_processCSI(f.buf, f.len);
            self->_processPendingModelOperation();
        }
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

    // DIAG: record the last STA disconnect reason for remote diagnostics. Read-only,
    // does not influence connect logic. Registered once (this init runs once at startup).
    WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t info){
        _lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t){
        _wifiScanDriverDone.store(true, std::memory_order_release);
    }, ARDUINO_EVENT_WIFI_SCAN_DONE);

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
    if (_isOtaInProgress()) {
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
    if (_wifiScanMutex == nullptr) {
        _wifiScanMutex = xSemaphoreCreateMutex();
        if (_wifiScanMutex == nullptr) {
            Serial.println("[CSI] WARNING: WiFi scan mutex allocation failed");
        }
    }
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
    snprintf(_tShadow,     sizeof(_tShadow),     "%s/model/shadow",    _topicPrefix);

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

    // Worker pro CSI pipeline — vytvořit před registrací callbacku, jinak
    // by první frame šel do neexistující fronty. Queue 8×258 B, task na
    // core 1 (core 0 patří WiFi driveru).
    if (_csiFrameQueue == nullptr) {
        _csiFrameQueue = xQueueCreate(8, sizeof(CsiFrame));
        if (_csiFrameQueue != nullptr &&
            xTaskCreatePinnedToCore(_csiProcTask, "csi_proc", 8192, this, 2,
                                    &_csiProcHandle, 1) != pdPASS) {
            vQueueDelete(_csiFrameQueue);
            _csiFrameQueue = nullptr;
            Serial.println("[CSI] csi_proc task create failed — CSI disabled");
        }
    }
    esp_wifi_set_csi_rx_cb(_csiCallback, this);
    esp_wifi_set_csi(true);

    _active = true;
    Serial.printf("[CSI] CSI capture enabled (window=%d, threshold=%.2f)\n",
                  _windowSize, _threshold);

    _loadLearnedModel();

    // csi-model: migrate the legacy single model into the active slot (once) and
    // mirror it into the runtime learned-* fields. This does NOT touch detection
    // _threshold — boot behavior stays identical to before; only apply/rollback
    // switch detection. Legacy csi_lrn_* keys are wiped once migration verified
    // the active slot (M-4); they are no longer written, so a downgrade to a
    // pre-slot firmware would boot without a learned model.
    _modelStore.attach(_prefs);
    _modelMgr.loadFromStore();
    if (!_modelMgr.active().valid && _siteModelReady) {
        CsiLegacyModel lg;
        lg.ok  = true;
        lg.thr = _learnedThreshold; lg.mu = _learnedMeanVar; lg.std = _learnedStdVar;
        lg.max = _learnedMaxVar;
        lg.turb = _learnedIdleMeanTurb; lg.ph = _learnedIdleMeanPhase; lg.amp = _learnedIdleAmpBaseline;
        lg.n   = _learnedSampleCount;
        if (_modelMgr.migrateLegacy(lg) == CsiModelOp::OK) {
            _removeLegacyKeys();   // verified in the active slot — legacy copy no longer needed
            Serial.printf("[CSI] Migrated legacy model -> active slot gen=%lu (legacy keys removed)\n",
                          (unsigned long)_modelMgr.active().generation);
        }
    }
    if (_modelMgr.active().valid) _applyActiveToRuntime();

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

void CSIService::_processCSI(const int8_t* bufIn, uint16_t lenIn) {
    const int8_t* buf = bufIn;
    int len = lenIn;

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
    if (_bufCount < _windowSize) {
        // Not enough samples yet — record an (invalid) trace so /api/csi/decision
        // reports why no decision is being made rather than stale data.
        _recordDecisionTrace(false, false, _motionState, false, 0, 0, getEffectiveThreshold());
        // v5.3.1: health transitions MUST be evaluated even with an empty window —
        // a starved CSI link (the reason the window is empty) is exactly the state
        // packet_rate_low exists to record. Overnight starvation on a weak-RSSI
        // node previously logged nothing at all.
        _updateHealthEvents(getEffectiveThreshold());
        return;
    }

    // v5.4: adaptive P95 replaces the configured threshold (may lower it),
    // clamped by the link-relative floor — see getEffectiveThreshold().
    float effThr = getEffectiveThreshold();

    bool prevMotion = _motionState;  // P1.3: detect motion edges for the event ring

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
            _recordDecisionTrace(true, rawMotion, _motionState, true, motionCount, _smoothCount, effThr);
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

    _recordDecisionTrace(true, rawMotion, _motionState, false, motionCount, _smoothCount, effThr);

    // P1.3: log motion edges and variance spikes to the diagnostic ring.
    if (_motionState != prevMotion) {
        _pushEvent(_motionState ? CsiEventType::MOTION_ENTER : CsiEventType::MOTION_EXIT, effThr);
    } else if (_runningVariance > SPIKE_VARIANCE_FACTOR * effThr) {
        uint32_t nowMs = millis();
        if (_lastSpikeEventMs == 0 || (nowMs - _lastSpikeEventMs) >= SPIKE_EVENT_MIN_GAP_MS) {
            _lastSpikeEventMs = nowMs;
            _pushEvent(CsiEventType::VARIANCE_SPIKE, effThr);
        }
    }

    // P1.1: shadow-evaluate the candidate in parallel (diagnostic only, must run
    // AFTER the active decision so disagreements compare against the real verdict).
    _updateShadow(effThr);

    // P1.4: log CSI-intrinsic health transitions to the diagnostic ring.
    _updateHealthEvents(effThr);

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

// P1.2: capture a read-only snapshot of the last motion decision. Purely
// diagnostic — never influences detection. Called at every exit of
// _updateMotionState() so /api/csi/decision explains the current verdict.
void CSIService::_recordDecisionTrace(bool bufferReady, bool rawMotion, bool finalMotion,
                                      bool breathHold, uint8_t votes, uint8_t window, float effThr) {
    CsiDecisionTrace& t = _decisionTrace;
    t.valid               = bufferReady;
    t.decision            = finalMotion;
    t.reason              = csiClassifyDecision(bufferReady, rawMotion, finalMotion, breathHold);
    t.variance            = _runningVariance;
    t.configuredThreshold = _threshold;
    t.adaptiveThreshold   = _adaptiveThresholdEnabled ? _adaptiveThreshold : 0.0f;
    t.effectiveThreshold  = effThr;
    t.hysteresisThreshold = effThr * _hysteresis;
    t.rawMotion           = rawMotion;
    t.smoothingVotes      = votes;
    t.smoothingWindow     = window;
    t.enterVotes          = SMOOTH_ENTER;
    t.exitVotes           = SMOOTH_EXIT;
    t.breathingHold       = breathHold;
    t.breathHoldCount     = _breathHoldCount;
    t.mlMotion            = _mlMotion;
    t.mlProbability       = _mlProbability;
    t.radarPresent        = _radarPresent;
    t.activeGeneration    = _modelMgr.active().generation;
    t.uptimeMs            = millis();
}

// P1.3: append a diagnostic event snapshot to the RAM ring. Shadow fields are
// filled for MODEL_DISAGREEMENT events (P1.1); defaulted otherwise.
void CSIService::_pushEvent(CsiEventType type, float effThr, float shadowThr,
                            bool shadowMotion, uint16_t healthFlags) {
    CsiEvent e;
    e.uptimeMs           = millis();
    e.type               = type;
    e.variance           = _runningVariance;
    e.effectiveThreshold = effThr;
    e.shadowThreshold    = shadowThr;
    e.activeMotion       = _motionState;
    e.shadowMotion       = shadowMotion;
    e.radarPresent       = _radarPresent;
    e.mlProbability      = _mlProbability;
    e.rssi               = (int16_t)getWifiRSSI();
    e.pps                = _packetRate;
    e.healthFlags        = healthFlags;
    _events.push(e);
}

// P1.4: CSI-intrinsic health flag subset. Radar and MQTT are external subsystems
// CSIService cannot see, so they are marked benign here (radarAvailable=true,
// mqttExpected=false) — the full picture (incl. radar/mqtt) is assembled in the
// /api/csi/health handler. This subset is what the sensor itself can log as it
// changes over time.
uint16_t CSIService::_computeCsiHealthFlags() const {
    CsiHealthInputs in;
    in.csiActive          = _active;
    in.htLtfSeen          = _htLtfSeen;
    in.packetRate         = _packetRate;
    in.packetRateFloorPps = 5.0f;
    in.packetRateUnstable = isPacketRateUnstable();
    in.wifiRoamedRecently = hasRoamedWithin(60000);
    const CsiSiteModel& a = _modelMgr.active();
    in.modelValid         = a.valid;
    uint32_t ep = (uint32_t)time(nullptr);
    in.clockValid         = ep > 1600000000u;
    in.modelStale         = in.clockValid && a.valid && a.createdAt > 0 && ep > a.createdAt &&
                            (ep - a.createdAt) > (30u * 24u * 3600u);
    uint32_t learnTotal   = _siteLearnAccepted + _siteLearnRejectedMotion + _siteLearnRejectedRadar;
    in.learningContaminated = _siteLearningActive && learnTotal >= 50 && learningRejectRatio() > 0.5f;
    in.radarAvailable     = true;    // external — excluded from the CSI-intrinsic subset
    in.mqttExpected       = false;   // external — excluded from the CSI-intrinsic subset
    in.mqttConnected      = true;
    in.mlSaturated        = _mlSatGuard.saturated();
    return csiHealthReasons(in);
}

// P1.4 (v5.3.1): log a HEALTH_CHANGE event when the CSI-intrinsic health flags
// change AND hold stable for ~10 ticks. The debounce keeps a boundary-oscillating
// flag (packet_rate_unstable flipping every tick filled the whole ring overnight)
// from evicting motion edges; genuine transitions still log exactly once.
void CSIService::_updateHealthEvents(float effThr) {
    uint16_t hf = _computeCsiHealthFlags();
    if (_healthDebounce.feed(hf)) {
        _pushEvent(CsiEventType::HEALTH_CHANGE, effThr, 0.0f, false, hf);
    }
}

// P1.1: run the candidate model's verdict in parallel with the active model.
// DIAGNOSTIC ONLY — reads _runningVariance/_hysteresis and writes only shadow
// members, so it can never mutate active detection state. Counters reset when
// the candidate changes (finalize/apply/rollback/clear) so they always belong
// to the candidate currently under evaluation.
void CSIService::_updateShadow(float effThr) {
    if (!hasCandidateModel()) { _shadowMotion = false; return; }

    uint32_t cg = modelCandidate().generation;
    if (cg != _shadowCandGen) {
        _shadow.reset();
        _shadowAgree = 0;
        _shadowDisagree = 0;
        _shadowMotion = false;
        _shadowCandGen = cg;
    }

    float shThr = modelCandidate().threshold;
    bool sh = _shadow.update(_runningVariance, shThr, _hysteresis);
    _shadowMotion = sh;

    if (sh == _motionState) {
        _shadowAgree++;
    } else {
        _shadowDisagree++;
        _pushEvent(CsiEventType::MODEL_DISAGREEMENT, effThr, shThr, sh);
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
    // v5.4: link-relative floor (was: hard absolute MIN_LEARNED_THRESHOLD)
    float relFloor = csiModelRelativeFloor(_learnedMeanVar);
    if (newThr < relFloor) newThr = relFloor;
    if (newThr < _threshold) newThr = _threshold;  // never drop below user-set sensitivity
    _learnedThreshold = newThr;

    _learnRefreshCount++;

    unsigned long now = millis();
    if (now - _lastLearnRefreshSaveMs > 600000UL) {  // 10 min
        // EMA drifts ONLY the active slot; candidate/previous stay immutable.
        // The slot is the source of truth now — no legacy csi_lrn_* write (M-4).
        _modelMgr.updateActiveEma(_learnedThreshold, _learnedMeanVar, _learnedStdVar);
        if (_prefs) _prefs->putFloat("csi_thr", _learnedThreshold);  // config-path sync
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
    // v5.4: below the usable capture floor, DSER/turbulence features run on a
    // packet-count time base stretched far past training — don't trust the vote.
    if (!csiMlVoteTrusted(_packetRate, ML_MIN_PACKET_RATE_PPS)) {
        _mlProbability = 0.0f;
        _mlMotion = false;
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

    // v5.4.1 (navrh 1A): duty-cycle saturační hlídač. Krmí se finálním
    // hlasem; když hlas visí na true >=95 % za 6 h, přestává se mu věřit —
    // _mlProbability zůstává v telemetrii (diagnostika), ale fusion přes
    // getMlMotionState() dostane false, dokud se hlas hodinu nechová.
    _mlSatGuard.tick(_mlMotion, millis());
    if (_mlSatGuard.saturated()) _mlMotion = false;
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

void CSIService::_publishDetectionSnapshot() {
    CsiDetectionSnapshot snapshot;
    snapshot.motion = _motionState;
    snapshot.mlMotion = _mlMotion;
    snapshot.mlEnabled = _mlEnabled;
    snapshot.compositeScore = getCompositeScore();
    snapshot.breathingScore = getBreathingScore();
    snapshot.mlProbability = _mlProbability;
    snapshot.mlThreshold = _mlThreshold;
    snapshot.dataOk = _detectionDataOk;
    _detectionSnapshot.publish(snapshot);
}

void CSIService::setDetectionDataOk(bool ok) {
    _detectionDataOk = ok;
    CsiDetectionSnapshot snapshot;
    if (_detectionSnapshot.read(snapshot)) {
        snapshot.dataOk = ok;
        _detectionSnapshot.publish(snapshot);
    }
}

// ============================================================================
// MQTT Publishing
// ============================================================================

void CSIService::_publishMQTT() {
    if (!_mqtt || !_mqtt->connected()) return;

    // Change-gated publishing: floats go out when they move by >5 % (plus a
    // small absolute floor for near-zero metrics), ON/OFF states on flip. A
    // 60 s heartbeat republishes everything so consumers survive a missed
    // message. Unconditional publishing at the 1 s tick pushed ~720 msg/min
    // of mostly unchanged values to the broker.
    uint32_t now = millis();
    bool force = !_pubValid || (uint32_t)(now - _mqttHeartbeatMs) >= 60000UL;
    if (force) _mqttHeartbeatMs = now;

    // Float metrics are inherently noisy — the deadband alone still fired
    // ~40 msg/min per topic at the 1 s tick. Pace them to one evaluation per
    // 10 s; ON/OFF states below stay per-tick so flips reach HA instantly.
    bool floatTick = force || (uint32_t)(now - _floatPaceMs) >= 10000UL;
    if (floatTick) _floatPaceMs = now;

    char val[16];
    auto pubFloat = [&](const char* topic, float v, float& last) {
        if (!floatTick) return;
        if (force || fabsf(v - last) > (0.05f * fabsf(last) + 1e-4f)) {
            snprintf(val, sizeof(val), "%.4f", v);
            if (_mqtt->publish(topic, val)) last = v;
        }
    };

    if (force || _motionState != _pubMotion) {
        if (_mqtt->publish(_tMotion, _motionState ? "ON" : "OFF", true))
            _pubMotion = _motionState;
    }

    pubFloat(_tTurbulence, _lastTurbulence,     _pubTurbulence);
    pubFloat(_tVariance,   _runningVariance,    _pubVariance);
    pubFloat(_tPhaseTurb,  _lastPhaseTurb,      _pubPhaseTurb);
    pubFloat(_tRatioTurb,  _lastRatioTurb,      _pubRatioTurb);
    pubFloat(_tBreathing,  getBreathingScore(), _pubBreathing);
    pubFloat(_tComposite,  getCompositeScore(), _pubComposite);
    pubFloat(_tDser,       _lastDser,           _pubDser);
    pubFloat(_tPlcr,       _lastPlcr,           _pubPlcr);

    // Monotonic counter — changes every tick by definition, heartbeat only.
    if (force) {
        snprintf(val, sizeof(val), "%lu", (unsigned long)_totalPackets);
        _mqtt->publish(_tPackets, val);
    }

    if (_mlEnabled) {
        pubFloat(_tMlProb, _mlProbability, _pubMlProb);
        if (force || _mlMotion != _pubMlMotion) {
            if (_mqtt->publish(_tMlMotion, _mlMotion ? "ON" : "OFF", true))
                _pubMlMotion = _mlMotion;
        }
    }

    // P1.1: retained shadow state — DIAGNOSTIC ONLY, explicitly marked so no
    // automation ever wires it to the alarm. Published on shadow flip or the
    // heartbeat, and only while a candidate is under evaluation.
    if (hasCandidateModel() && (force || _shadowMotion != _pubShadowMotion)) {
        char js[256];
        snprintf(js, sizeof(js),
            "{\"note\":\"SHADOW - NO ALARM EFFECT\",\"active\":%s,\"shadow\":%s,"
            "\"agree\":%lu,\"disagree\":%lu,\"variance\":%.5f,"
            "\"active_thr\":%.5f,\"shadow_thr\":%.5f,\"candidate_gen\":%lu}",
            _motionState ? "true" : "false", _shadowMotion ? "true" : "false",
            (unsigned long)_shadowAgree, (unsigned long)_shadowDisagree,
            _runningVariance, getEffectiveThreshold(), modelCandidate().threshold,
            (unsigned long)modelCandidate().generation);
        if (_mqtt->publish(_tShadow, js, true)) _pubShadowMotion = _shadowMotion;
    }

    _pubValid = true;
}

// ============================================================================
// Main loop update
// ============================================================================

void CSIService::update() {
    if (!_active) return;

    // A scan hops across channels, so CSI capture is suspended for its short
    // duration. Poll before every early return to guarantee capture is restored
    // even if WiFi disconnected or an OTA window opened while scanning.
    _pollWifiScan();
    uint32_t scanNow = millis();
    if (getWifiScanState() != WifiScanState::RUNNING &&
        _wifiScanCsiSuspended.load(std::memory_order_acquire) &&
        scanNow - _wifiScanRestoreAttemptMs >= 1000) {
        _wifiScanRestoreAttemptMs = scanNow;
        if (_restoreCsiAfterWifiScan()) {  // retry a rare failed CSI restore
            _finishWifiScanOperation();
        }
    }
    _publishWifiScanMqtt();
    if (getWifiScanState() == WifiScanState::RUNNING) return;

    // csi7b: Pause all WiFi/lwIP manipulation while an OTA transfer is active.
    // WiFi.reconnect(), netif_set_default() and raw_sendto traffic gen all risk
    // dropping the in-flight OTA TCP stream. Hooks in main.cpp / WebRoutes.cpp
    // set this flag at OTA start and clear it at end/error.
    if (_isOtaInProgress()) return;

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
            _reconnectAttempts++;
            // Out-of-coverage visibility: previously this loop was silent and the
            // 30s diag below was skipped by the early return, so serial debug went
            // quiet exactly when WiFi dropped. Log each attempt with the raw WL_*
            // status and last disconnect reason (200=BEACON_TIMEOUT/out-of-range,
            // 201=NO_AP_FOUND, 15/205=bad PSK, 2/202=auth).
            Serial.printf("[CSI] WiFi DOWN (status=%d last_reason=%u) — reconnect attempt #%lu\n",
                          (int)WiFi.status(), (unsigned)_lastDisconnectReason,
                          (unsigned long)_reconnectAttempts);
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
        // P1.4 health: slow EMA of the capture rate for the stability check.
        _packetRateEma = (_packetRateEma <= 0.0f) ? _packetRate
                                                  : 0.2f * _packetRate + 0.8f * _packetRateEma;
        _lastPublishMs = now;
        _updateMotionState();
        _runMlInference();
        _publishDetectionSnapshot();
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
                _finishCalibrationOperation();
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
                    if (calibrateThreshold(10000)) {
                        Serial.printf("[CSI] Auto-cal: quiet for %us — triggering recalibration\n",
                                      _autoCalQuietSeconds);
                        _autoCalDone = true;
                    } else {
                        _autoCalQuietStart = now;  // busy: retry after another quiet window
                    }
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
                    _lastBssidChangeMs = millis();  // P1.4 health: mark recent roam
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
                        if (_runtimeOperationCoordinator != nullptr) {
                            _runtimeOperationCoordinator->markProgress(
                                RuntimeOperation::SITE_LEARNING, _siteLearnStartMs);
                        }
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
                _varHist.add(_runningVariance);   // quality-report quantiles
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

void CSIService::_finishWifiScanOperation() {
    if (_runtimeOperationCoordinator != nullptr) {
        _runtimeOperationCoordinator->finish(RuntimeOperation::WIFI_SCAN);
    }
}

void CSIService::_finishCalibrationOperation() {
    if (_runtimeOperationCoordinator != nullptr) {
        _runtimeOperationCoordinator->finish(RuntimeOperation::CALIBRATION);
    }
}

void CSIService::_finishSiteLearningOperation(bool cancelled) {
    if (_runtimeOperationCoordinator == nullptr) return;
    if (cancelled) {
        _runtimeOperationCoordinator->cancel(RuntimeOperation::SITE_LEARNING);
    } else {
        _runtimeOperationCoordinator->finish(RuntimeOperation::SITE_LEARNING);
    }
}

void CSIService::_pollRuntimeOperationTimeouts(uint32_t nowMs) {
    if (_runtimeOperationCoordinator == nullptr) return;
    RuntimeOperationStatus timedOut;
    if (_calibrating && _runtimeOperationCoordinator->checkTimeout(
            RuntimeOperation::CALIBRATION, nowMs, &timedOut)) {
        _calibrating = false;
        Serial.println("[CSI] Calibration timed out — runtime claim released");
    }
    if (_siteLearningActive && _runtimeOperationCoordinator->checkTimeout(
            RuntimeOperation::SITE_LEARNING, nowMs, &timedOut)) {
        _siteLearningActive = false;
        Serial.println("[CSI] Site learning timed out — runtime claim released");
    }
    if (_runtimeOperationCoordinator->checkTimeout(
            RuntimeOperation::WIFI_SCAN, nowMs, &timedOut)) {
        _wifiScanStartPending.store(false, std::memory_order_release);
        if (getWifiScanState() == WifiScanState::RUNNING) esp_wifi_scan_stop();
        _wifiScanDurationMs.store(getWifiScanElapsedMs(), std::memory_order_release);
        _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::TIMEOUT),
                               std::memory_order_release);
        _wifiScanState.store(static_cast<uint8_t>(WifiScanState::FAILED),
                             std::memory_order_release);
        _wifiScanMqttDirty.store(true, std::memory_order_release);
        _restoreCsiAfterWifiScan();
        Serial.println("[CSI] WiFi scan timed out — runtime claim released");
    }
}
WifiScanStartResult CSIService::startWifiScan() {
    if (!_active || _wifiScanMutex == nullptr) {
        return WifiScanStartResult::FAILED;
    }
    if (_isOtaInProgress() || _siteLearningActive || _calibrating) {
        return WifiScanStartResult::BUSY;
    }
    if (_runtimeOperationCoordinator != nullptr &&
        !_runtimeOperationCoordinator->tryBegin(
            RuntimeOperation::WIFI_SCAN, millis(), 16000)) {
        return WifiScanStartResult::BUSY;
    }
    if (xSemaphoreTake(_wifiScanMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        _finishWifiScanOperation();
        return WifiScanStartResult::FAILED;
    }

    if (getWifiScanState() == WifiScanState::RUNNING) {
        xSemaphoreGive(_wifiScanMutex);
        _finishWifiScanOperation();
        return WifiScanStartResult::BUSY;
    }

    WiFi.scanDelete();
    _wifiScanResults.clear();
    _wifiScanStartMs.store(millis(), std::memory_order_relaxed);
    _wifiScanDurationMs.store(0, std::memory_order_release);
    _wifiScanResultCount.store(0, std::memory_order_release);
    _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::NONE),
                           std::memory_order_release);
    _wifiScanDriverDone.store(false, std::memory_order_release);
    _wifiScanStartPending.store(false, std::memory_order_release);
    _wifiScanStartAttempts = 0;

    // Frames received while the STA visits foreign channels must never enter
    // the detector/model baseline. The callback/config stay registered and are
    // re-enabled by _pollWifiScan() after completion or timeout.
    esp_err_t pauseErr = esp_wifi_set_csi(false);
    if (pauseErr != ESP_OK) {
        _wifiScanFailure.store(
            static_cast<uint8_t>(WifiScanFailureReason::CSI_PAUSE_FAILED),
            std::memory_order_release);
        _wifiScanState.store(static_cast<uint8_t>(WifiScanState::FAILED),
                             std::memory_order_release);
        _wifiScanCaptureActive.store(true, std::memory_order_release);
        _wifiScanMqttDirty.store(true, std::memory_order_release);
        xSemaphoreGive(_wifiScanMutex);
        _finishWifiScanOperation();
        Serial.printf("[CSI] WiFi scan refused: failed to suspend CSI (0x%x)\n",
                      pauseErr);
        return WifiScanStartResult::FAILED;
    }
    // Publish RUNNING before exposing the suspended flag. update() observes
    // both lock-free; the reverse order leaves a tiny window where it sees an
    // old COMPLETE/FAILED state and immediately restores CSI mid-scan.
    _wifiScanCaptureActive.store(false, std::memory_order_release);
    _wifiScanState.store(static_cast<uint8_t>(WifiScanState::RUNNING),
                         std::memory_order_release);
    _wifiScanCsiSuspended.store(true, std::memory_order_release);
    _wifiScanMqttDirty.store(true, std::memory_order_release);
    // The AsyncWebServer callback may arrive while the STA is reconnecting.
    // ESP-IDF rejects a scan started in that state. Cancel only an already
    // disconnected/in-flight association, then let update() start the scan.
    _wifiScanAutoReconnectSuspended.store(true, std::memory_order_release);
    WiFi.setAutoReconnect(false);
    bool connected = WiFi.status() == WL_CONNECTED;
    if (!connected) WiFi.disconnect(false);
    _wifiScanNextStartAttemptMs = millis() + (connected ? 0 : 350);
    _wifiScanStartPending.store(true, std::memory_order_release);
    xSemaphoreGive(_wifiScanMutex);
    Serial.println("[CSI] WiFi AP scan queued");
    return WifiScanStartResult::STARTED;
}

uint8_t CSIService::copyWifiScanResults(WifiScanNetwork* out, uint8_t capacity) {
    if (_wifiScanMutex == nullptr || out == nullptr || capacity == 0) return 0;
    if (xSemaphoreTake(_wifiScanMutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    uint8_t count = _wifiScanResults.copyTo(out, capacity);
    xSemaphoreGive(_wifiScanMutex);
    return count;
}

bool CSIService::_restoreCsiAfterWifiScan() {
    bool restored = true;
    if (_wifiScanCsiSuspended.load(std::memory_order_acquire)) {
        esp_err_t err = esp_wifi_set_csi(true);
        if (err != ESP_OK) {
            Serial.printf("[CSI] WARNING: Failed to restore CSI after scan: 0x%x\n", err);
            restored = false;
            _wifiScanCaptureActive.store(false, std::memory_order_release);
        } else {
            _wifiScanCsiSuspended.store(false, std::memory_order_release);
            _wifiScanCaptureActive.store(true, std::memory_order_release);
        }
        _wifiScanMqttDirty.store(true, std::memory_order_release);
    }

    // Exclude the intentional capture gap from packet-rate health and resume
    // Ethernet as the outbound default if the scan changed lwIP preference.
    _windowPackets = 0;
    _lastPublishMs = millis();
    if (!_isOtaInProgress()) _restoreEthDefaultNetif();
    if (_wifiScanAutoReconnectSuspended.exchange(false, std::memory_order_acq_rel)) {
        WiFi.setAutoReconnect(true);
        if (WiFi.status() != WL_CONNECTED) _reconnectRequested = true;
    }
    return restored;
}

void CSIService::_publishWifiScanMqtt() {
    if (_mqtt == nullptr || !_mqtt->connected()) return;

    WifiScanState state = getWifiScanState();
    uint32_t now = millis();
    uint32_t interval = (state == WifiScanState::RUNNING) ? 1000UL : 60000UL;
    bool dirty = _wifiScanMqttDirty.exchange(false, std::memory_order_acq_rel);
    if (!dirty && now - _wifiScanMqttPublishMs < interval) return;

    uint32_t duration = (state == WifiScanState::RUNNING)
        ? getWifiScanElapsedMs() : getWifiScanDurationMs();
    WifiScanFailureReason reason = getWifiScanFailureReason();
    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\",\"capture_active\":%s,\"duration_ms\":%lu,"
             "\"result_count\":%u,\"reason\":\"%s\"}",
             wifiScanStateText(state), isWifiCaptureActive() ? "true" : "false",
             (unsigned long)duration, (unsigned)getWifiScanResultCount(),
             wifiScanFailureText(reason));

    if (_mqtt->publish(_mqtt->getTopics().csi_wifi_scan, payload, true)) {
        _wifiScanMqttPublishMs = now;
    } else {
        // Retry the current snapshot after MQTT reconnect; do not intentionally
        // enqueue historical scan transitions while the broker is offline.
        _wifiScanMqttDirty.store(true, std::memory_order_release);
    }
}

void CSIService::_pollWifiScan() {
    if (getWifiScanState() != WifiScanState::RUNNING) return;

    // Do not start a scan from the HTTP task while WiFi is in its reconnect
    // transition. The main loop owns the delayed start and retries a busy
    // driver a few times before reporting a real failure.
    if (_wifiScanStartPending.load(std::memory_order_acquire)) {
        uint32_t now = millis();
        if (now < _wifiScanNextStartAttemptMs) return;

        // Arduino-ESP32 derives a realistic async scan deadline from the
        // per-channel dwell; 300 ms gives the complete band enough time.
        int16_t result = WiFi.scanNetworks(true, false, false, 300);
        if (result != WIFI_SCAN_FAILED) {
            _wifiScanStartPending.store(false, std::memory_order_release);
            _wifiScanDriverDone.store(false, std::memory_order_release);
            Serial.println("[CSI] Asynchronous WiFi AP scan started");
            return;
        }

        _wifiScanStartAttempts++;
        if (_wifiScanStartAttempts < 5 && getWifiScanElapsedMs() < 5000) {
            _wifiScanNextStartAttemptMs = now + 300;
            Serial.printf("[CSI] WiFi scan driver busy; retry %u/5\n",
                          (unsigned)_wifiScanStartAttempts);
            return;
        }

        _wifiScanStartPending.store(false, std::memory_order_release);
        _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::DRIVER_START_FAILED),
                               std::memory_order_release);
        _wifiScanDurationMs.store(getWifiScanElapsedMs(), std::memory_order_release);
        bool restored = _restoreCsiAfterWifiScan();
        if (!restored) {
            _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::CSI_RESTORE_FAILED),
                                   std::memory_order_release);
        }
        _wifiScanState.store(static_cast<uint8_t>(WifiScanState::FAILED),
                             std::memory_order_release);
        _wifiScanMqttDirty.store(true, std::memory_order_release);
        if (restored) _finishWifiScanOperation();
        Serial.println("[CSI] WiFi scan failed to start after retries");
        return;
    }

    bool driverDone = _wifiScanDriverDone.load(std::memory_order_acquire);
    bool timedOut = getWifiScanElapsedMs() > 15000;
    if (!driverDone && !timedOut) return;

    // WiFi.scanComplete() applies an additional max_ms_per_chan * 20 timeout
    // which may expire before a connected STA receives WIFI_SCAN_DONE. Query it
    // only after the real driver event so that wrapper timeout cannot abort a
    // scan that is still making progress.
    int16_t count = driverDone ? WiFi.scanComplete() : WIFI_SCAN_FAILED;
    if (driverDone && count == WIFI_SCAN_RUNNING) return;

    if (xSemaphoreTake(_wifiScanMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (getWifiScanState() != WifiScanState::RUNNING) {
        xSemaphoreGive(_wifiScanMutex);
        return;
    }

    if (timedOut && !driverDone) {
        esp_wifi_scan_stop();
        count = WIFI_SCAN_FAILED;
        _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::TIMEOUT),
                               std::memory_order_release);
    }

    _wifiScanResults.clear();
    if (count >= 0) {
        String currentSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String();
        for (int16_t i = 0; i < count; i++) {
            String ssid = WiFi.SSID(i);
            _wifiScanResults.add(
                ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i),
                static_cast<uint8_t>(WiFi.encryptionType(i)),
                currentSsid.length() > 0 && ssid == currentSsid);
        }
    }

    uint8_t resultCount = _wifiScanResults.count();
    WiFi.scanDelete();
    _wifiScanResultCount.store(resultCount, std::memory_order_release);
    _wifiScanDurationMs.store(getWifiScanElapsedMs(), std::memory_order_release);
    bool restored = _restoreCsiAfterWifiScan();
    WifiScanState state;
    if (!restored) {
        state = WifiScanState::FAILED;
        _wifiScanFailure.store(
            static_cast<uint8_t>(WifiScanFailureReason::CSI_RESTORE_FAILED),
            std::memory_order_release);
    } else if (count >= 0) {
        state = WifiScanState::READY;
        _wifiScanFailure.store(static_cast<uint8_t>(WifiScanFailureReason::NONE),
                               std::memory_order_release);
    } else {
        state = WifiScanState::FAILED;
        if (getWifiScanFailureReason() == WifiScanFailureReason::NONE) {
            _wifiScanFailure.store(
                static_cast<uint8_t>(WifiScanFailureReason::DRIVER_FAILED),
                std::memory_order_release);
        }
    }
    _wifiScanState.store(static_cast<uint8_t>(state), std::memory_order_release);
    _wifiScanMqttDirty.store(true, std::memory_order_release);
    if (restored) _finishWifiScanOperation();
    xSemaphoreGive(_wifiScanMutex);

    if (state == WifiScanState::READY) {
        Serial.printf("[CSI] WiFi AP scan complete: %u unique SSIDs\n", resultCount);
    } else {
        Serial.println("[CSI] WiFi AP scan failed or timed out");
    }
}

void CSIService::wifiDownForOta() {
    // Single-home to Ethernet for OTA. A CSI WiFi STA sharing the Ethernet subnet
    // makes the box dual-homed → ambiguous return path → espota auth/upload stalls.
    // Stop traffic gen and drop the AP association; keep STA mode + CSI config so a
    // later wifiUpAfterOta() (or a fresh boot) brings it straight back. update()'s
    // reconnect loop is gated by the runtime coordinator, and we disable auto-reconnect, so
    // WiFi stays down for the whole OTA window.
    _stopTrafficGen();
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    Serial.println("[CSI] WiFi STA dropped for OTA (single-homing to Ethernet)");
}

void CSIService::wifiUpAfterOta() {
    // Restore after an OTA window that closed without a reboot (timeout / abort).
    // A successful flash reboots, so WiFi comes back via begin() on boot instead.
    WiFi.setAutoReconnect(true);
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    Serial.println("[CSI] WiFi STA restore requested after OTA window");
}

bool CSIService::calibrateThreshold(uint32_t durationMs) {
    if (durationMs < 1000) durationMs = 1000;
    if (durationMs > 60000) durationMs = 60000;
    uint32_t now = millis();
    if (_runtimeOperationCoordinator != nullptr &&
        !_runtimeOperationCoordinator->tryBegin(
            RuntimeOperation::CALIBRATION, now, durationMs + 5000UL)) {
        return false;
    }
    _calibrating = true;
    _calibStartMs = now;
    _calibDurationMs = durationMs;
    _calibVarSum = 0.0f;
    _calibSamples = 0;
    Serial.printf("[CSI] Calibration started — sampling %u ms (keep area still)\n", durationMs);
    return true;
}

float CSIService::getCalibrationProgress() const {
    if (!_calibrating || _calibDurationMs == 0) return 0.0f;
    uint32_t elapsed = millis() - _calibStartMs;
    if (elapsed >= _calibDurationMs) return 1.0f;
    return (float)elapsed / (float)_calibDurationMs;
}

// v5.4: adaptive P95 REPLACES the configured threshold once warmed up (it may
// go lower — that is the fix for the strong-link blindness), clamped by the
// link-relative floor. Adaptive off/cold -> explicit configured threshold.
float CSIService::getEffectiveThreshold() const {
    return csiEffectiveThreshold(_threshold, _adaptiveThreshold,
                                 _adaptiveThresholdEnabled && _adaptiveThreshold > 0.0f,
                                 _relativeFloor());
}

// Link-relative threshold floor: 3x the learned quiet baseline of THIS link
// (absolute sanity bound when no model exists yet). Replaces the old global
// MIN_LEARNED_THRESHOLD=0.005, which sat above walking peaks on strong links.
float CSIService::_relativeFloor() const {
    return csiModelRelativeFloor(_siteModelReady ? _learnedMeanVar : 0.0f);
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

bool CSIService::startSiteLearning(uint32_t durationMs, bool replaceCandidate) {
    if (durationMs < 60000) durationMs = 60000;
    if (durationMs > 604800000UL) durationMs = 604800000UL; // 7 days

    // A finished-but-unapplied candidate would be silently overwritten by a new
    // run; require an explicit replace so the caller can map this to HTTP 409.
    if (_modelMgr.hasCandidate() && !replaceCandidate) {
        Serial.println("[CSI] Site learning refused: candidate exists (pass replace_candidate=1)");
        return false;
    }
    uint32_t now = millis();
    if (_runtimeOperationCoordinator != nullptr &&
        !_runtimeOperationCoordinator->tryBegin(
            RuntimeOperation::SITE_LEARNING, now, durationMs + 60000UL)) {
        return false;
    }

    if (_modelMgr.hasCandidate() && replaceCandidate) {
        _modelMgr.clearCandidate();
    }

    resetIdleBaseline();
    _varHist.reset();

    _siteLearningActive = true;
    _siteLearnStartMs = now;
    _siteLearnDurationMs = durationMs;
    _siteLearnAccepted = 0;
    _siteLearnRejectedMotion = 0;
    _siteLearnRejectedRadar = 0;
    _siteLearnBssidResetCount = 0;
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
    _finishSiteLearningOperation(true);
    Serial.println("[CSI] Site learning stopped");
}

CsiModelOp CSIService::clearLearnedSiteModel() {
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

    _removeLegacyKeys();

    // Wipe the three-slot model store (active/candidate/previous) too — the
    // legacy csi_lrn_* removal above does not touch it, so without this the
    // active model survives in NVS+RAM and is reloaded on reboot (bug M-1).
    // BA-10: propagate the erase result so the API reports 500 instead of a
    // false "cleared" when NVS erase fails and the model would resurrect.
    CsiModelOp r = _modelMgr.factoryClear();
    // Drop adaptive-P95/smoothing buffers so stale short-term state doesn't
    // survive a clear and bias the runtime-fallback threshold.
    _resetShortTermState();
    if (r == CsiModelOp::OK) {
        _scheduleModelStatePublish(ModelPublishEvent::ALL_CLEARED);
    }

    Serial.printf("[CSI] Learned site model cleared (store=%s)\n",
                  r == CsiModelOp::OK ? "ok" : "STORE_FAILED");
    return r;
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

// M-4: wipe the legacy single-model NVS keys. Called once after a successful
// migration into the active slot, and on an explicit clear. The three-slot store
// (csi_model_*) is the sole model persistence now — legacy keys are no longer
// written, so downgrade to a pre-slot firmware would find no learned model.
void CSIService::_removeLegacyKeys() {
    if (!_prefs) return;
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

float CSIService::_computeSiteLearningThreshold() const {
    if (_siteLearnAccepted == 0) return _threshold;

    float variance = (_siteLearnAccepted > 1)
        ? (_siteLearnM2Var / (float)(_siteLearnAccepted - 1))
        : 0.0f;
    float stddev = sqrtf(variance);
    float candidate = _siteLearnMeanVarAcc + 6.0f * stddev;
    float maxGuard = _siteLearnMaxVar * 1.15f;

    if (candidate < maxGuard) candidate = maxGuard;
    // v5.4: link-relative floor. The 2026-04-24 hair-trigger came from a noisy
    // site where mean variance was high — 3x that mean still blocks it, while a
    // clean strong link keeps its (much lower) genuine sensitivity.
    float relFloor = csiModelRelativeFloor(_siteLearnMeanVarAcc);
    if (candidate < relFloor) candidate = relFloor;
    if (candidate > 100.0f) candidate = 100.0f;
    return candidate;
}

void CSIService::_finalizeSiteLearning() {
    _siteLearningActive = false;

    if (_siteLearnAccepted < 30) {
        Serial.printf("[CSI] Site learning aborted: only %lu quiet samples\n",
                      (unsigned long)_siteLearnAccepted);
        _finishSiteLearningOperation();
        return;
    }

    float variance = (_siteLearnAccepted > 1)
        ? (_siteLearnM2Var / (float)(_siteLearnAccepted - 1))
        : 0.0f;
    float stddev = sqrtf(variance);

    // Mirror _computeSiteLearningThreshold() but record which clamp bound the result,
    // so the quality report can explain a value like 0.005 (data vs. absolute floor).
    float candidate = _siteLearnMeanVarAcc + 6.0f * stddev;
    float maxGuard  = _siteLearnMaxVar * 1.15f;
    CsiClampReason clamp = CsiClampReason::NONE;
    if (candidate < maxGuard)              { candidate = maxGuard; clamp = CsiClampReason::MAXIMUM_LIMIT; }
    // v5.4: relative floor (3x quiet mean of THIS learning run) — reported as
    // ABSOLUTE_FLOOR in the quality report when it binds.
    float relFloor = csiModelRelativeFloor(_siteLearnMeanVarAcc);
    if (candidate < relFloor)              { candidate = relFloor; clamp = CsiClampReason::ABSOLUTE_FLOOR; }
    if (candidate > 100.0f) candidate = 100.0f;
    float threshold = candidate;

    // Build the candidate model — the active model and detection _threshold are
    // left UNCHANGED. The new result must be explicitly applied later.
    CsiSiteModel cand;
    cand.valid                 = true;
    cand.generation            = _modelMgr.nextGeneration();
    cand.sampleCount           = _siteLearnAccepted;
    cand.rejectedMotion        = _siteLearnRejectedMotion;
    cand.rejectedRadar         = _siteLearnRejectedRadar;
    cand.bssidResetCount       = _siteLearnBssidResetCount;
    cand.durationSec           = _siteLearnDurationMs / 1000;
    { uint32_t ep = (uint32_t)time(nullptr); cand.createdAt = (ep > 1600000000u) ? ep : 0; }  // epoch if NTP synced
    cand.threshold             = threshold;
    cand.meanVariance          = _siteLearnMeanVarAcc;
    cand.stdVariance           = stddev;
    cand.maxVariance           = _siteLearnMaxVar;
    cand.idleMeanTurbulence    = _idleMeanTurb;
    cand.idleMeanPhase         = _idleMeanPhase;
    cand.idleAmplitudeBaseline = _idleAmpBaseline;
    if (_bssidInitialized) memcpy(cand.bssid, _lastBSSID, 6);

    CsiModelOp r = _modelMgr.finalizeCandidate(cand);
    if (r != CsiModelOp::OK) {
        Serial.printf("[CSI] Candidate finalize failed (op=%d) — active unchanged\n", (int)r);
        _finishSiteLearningOperation();
        return;
    }

    // Quality report stored alongside the candidate (variance quantiles + clamp reason).
    CsiModelQuality q;
    q.generation           = cand.generation;
    q.accepted             = _siteLearnAccepted;
    q.rejectedMotion       = _siteLearnRejectedMotion;
    q.rejectedRadar        = _siteLearnRejectedRadar;
    q.p50 = _varHist.quantile(0.50f);
    q.p90 = _varHist.quantile(0.90f);
    q.p95 = _varHist.quantile(0.95f);
    q.p99 = _varHist.quantile(0.99f);
    q.mean = _siteLearnMeanVarAcc; q.std = stddev; q.max = _siteLearnMaxVar;
    q.thresholdClampReason = (uint8_t)clamp;
    csiQualitySeal(q);
    _lastQuality = q;
    _hasQuality  = true;
    _scheduleModelStatePublish(ModelPublishEvent::CANDIDATE_READY);

    Serial.printf(
        "[CSI] Site learning -> CANDIDATE gen=%lu quiet=%lu rejected(m/r)=%lu/%lu thr=%.6f (active unchanged)\n",
        (unsigned long)cand.generation,
        (unsigned long)_siteLearnAccepted,
        (unsigned long)_siteLearnRejectedMotion,
        (unsigned long)_siteLearnRejectedRadar,
        threshold
    );
    _finishSiteLearningOperation();
}

// ---- csi-model: runtime application of slots ------------------------------

// Model management state topics — informational only, separate from the alarm/
// ml_motion flow. Retained state is republished only on a model change (finalize/
// apply/rollback/clear), never per-tick, so it does not add to MQTT churn.
void CSIService::_publishModelState(const char* event) {
    if (!_mqtt || !_mqtt->connected()) return;
    char topic[96], buf[176];
    const CsiSiteModel& a = _modelMgr.active();
    snprintf(buf, sizeof(buf),
             "{\"valid\":%s,\"generation\":%lu,\"threshold\":%.5f,\"samples\":%lu}",
             a.valid ? "true" : "false", (unsigned long)a.generation, a.threshold,
             (unsigned long)a.sampleCount);
    snprintf(topic, sizeof(topic), "%s/model/active", _topicPrefix);
    _mqtt->publish(topic, buf, true);

    const CsiSiteModel& c = _modelMgr.candidate();
    snprintf(buf, sizeof(buf),
             "{\"valid\":%s,\"generation\":%lu,\"threshold\":%.5f,\"samples\":%lu}",
             c.valid ? "true" : "false", (unsigned long)c.generation, c.threshold,
             (unsigned long)c.sampleCount);
    snprintf(topic, sizeof(topic), "%s/model/candidate", _topicPrefix);
    _mqtt->publish(topic, buf, true);

    if (event) {
        snprintf(topic, sizeof(topic), "%s/model/event", _topicPrefix);
        _mqtt->publish(topic, event, false);
    }
}

void CSIService::processDeferredActions() {
    // Called unconditionally from loop(), including when csi_enabled was
    // toggled off but the user has not rebooted yet. Keep shared runtime claims
    // from being stranded in that interval.
    _pollRuntimeOperationTimeouts(millis());
    ModelPublishEvent event = static_cast<ModelPublishEvent>(
        _pendingModelPublish.exchange(static_cast<uint8_t>(ModelPublishEvent::NONE),
                                      std::memory_order_acq_rel));
    if (event == ModelPublishEvent::NONE) return;

    const char* eventText = nullptr;
    switch (event) {
        case ModelPublishEvent::CANDIDATE_READY:     eventText = "candidate_ready"; break;
        case ModelPublishEvent::APPLIED:             eventText = "applied"; break;
        case ModelPublishEvent::ROLLEDBACK:          eventText = "rolledback"; break;
        case ModelPublishEvent::CANDIDATE_CLEARED:  eventText = "candidate_cleared"; break;
        case ModelPublishEvent::CANDIDATE_IMPORTED: eventText = "candidate_imported"; break;
        case ModelPublishEvent::ALL_CLEARED:         eventText = "cleared"; break;
        case ModelPublishEvent::NONE: return;
    }
    _publishModelState(eventText);
}

void CSIService::_applyActiveToRuntime() {
    const CsiSiteModel& a = _modelMgr.active();
    if (!a.valid) return;
    _siteModelReady         = true;
    _learnedThreshold       = a.threshold;
    _learnedMeanVar         = a.meanVariance;
    _learnedStdVar          = a.stdVariance;
    _learnedMaxVar          = a.maxVariance;
    _learnedSampleCount     = a.sampleCount;
    _learnedIdleMeanTurb    = a.idleMeanTurbulence;
    _learnedIdleMeanPhase   = a.idleMeanPhase;
    _learnedIdleAmpBaseline = a.idleAmplitudeBaseline;
    if (a.idleMeanTurbulence > 0.0f || a.idleMeanPhase > 0.0f) {
        _idleMeanTurb    = a.idleMeanTurbulence;
        _idleMeanPhase   = a.idleMeanPhase;
        _idleAmpBaseline = a.idleAmplitudeBaseline;
        _idleInitialized = true;
    }
}

void CSIService::_resetShortTermState() {
    // Flush adaptive-P95, smoothing history and running variance so the new model
    // never decides on variance that was mixed from the previous baseline.
    _p95BufIndex = 0; _p95BufCount = 0; _adaptiveThreshold = 0.0f; _p95TickSinceUpdate = 0;
    _smoothHistory = 0; _smoothCount = 0;
    _bufCount = 0; _bufIndex = 0;
    _runningMean = 0; _runningM2 = 0; _runningVariance = 0;
    _breathHoldCount = 0;
    _lastLearnRefreshSaveMs = millis();   // reinit EMA save cadence for the new active
}

void CSIService::_switchDetectionToActive() {
    const CsiSiteModel& a = _modelMgr.active();
    if (!a.valid) return;
    _applyActiveToRuntime();
    setThreshold(a.threshold);            // moves _threshold + _baseThreshold, resets stuck counters
    _resetShortTermState();
    if (_prefs) _prefs->putFloat("csi_thr", a.threshold);
}

CsiModelOp CSIService::applyCandidateModel(bool force) {
    // M-2: a candidate learned on a different AP would apply a mismatched idle
    // fingerprint — block it unless the operator consciously overrides (force=1).
    if (!force && _modelMgr.hasCandidate() &&
        modelCompatibility(_modelMgr.candidate()) == CsiModelCompat::INCOMPATIBLE) {
        Serial.println("[CSI] Apply blocked — candidate BSSID incompatible with current AP (pass force=1)");
        return CsiModelOp::INCOMPATIBLE_AP;
    }
    CsiModelOp r = _modelMgr.applyCandidate();
    if (r == CsiModelOp::OK) {
        _switchDetectionToActive();
        _scheduleModelStatePublish(ModelPublishEvent::APPLIED);
        Serial.printf("[CSI] Applied candidate -> active gen=%lu thr=%.5f\n",
                      (unsigned long)_modelMgr.active().generation, _modelMgr.active().threshold);
    }
    return r;
}

CsiModelOp CSIService::rollbackSiteModel() {
    CsiModelOp r = _modelMgr.rollback();
    if (r == CsiModelOp::OK) {
        _switchDetectionToActive();
        _scheduleModelStatePublish(ModelPublishEvent::ROLLEDBACK);
        Serial.printf("[CSI] Rolled back -> active gen=%lu thr=%.5f\n",
                      (unsigned long)_modelMgr.active().generation, _modelMgr.active().threshold);
    }
    return r;
}

CsiModelOp CSIService::clearCandidateModel() {
    CsiModelOp r = _modelMgr.clearCandidate();
    if (r == CsiModelOp::OK) _scheduleModelStatePublish(ModelPublishEvent::CANDIDATE_CLEARED);
    return r;
}

CsiModelOp CSIService::_executeModelCommand(CsiModelCommand command, bool force) {
    switch (command) {
        case CsiModelCommand::APPLY:           return applyCandidateModel(force);
        case CsiModelCommand::ROLLBACK:        return rollbackSiteModel();
        case CsiModelCommand::CLEAR_CANDIDATE: return clearCandidateModel();
        case CsiModelCommand::CLEAR_ALL:       return clearLearnedSiteModel();
    }
    return CsiModelOp::STORE_FAILED;
}

void CSIService::_processPendingModelOperation() {
    CsiModelCommand command;
    bool force = false;
    if (!_modelCommandSlot.claim(command, force)) return;
    _modelCommandSlot.complete(_executeModelCommand(command, force));
}

CsiModelRequestStatus CSIService::requestModelOperation(CsiModelCommand command,
                                                        bool force,
                                                        uint32_t timeoutMs,
                                                        CsiModelOp& result) {
    if (!_active || _csiProcHandle == nullptr) {
        result = _executeModelCommand(command, force);
        return CsiModelRequestStatus::COMPLETED;
    }
    if (!_modelCommandSlot.submit(command, force)) {
        return CsiModelRequestStatus::BUSY;
    }

    uint32_t started = millis();
    do {
        if (_modelCommandSlot.poll(result)) return CsiModelRequestStatus::COMPLETED;
        delay(1);
    } while ((uint32_t)(millis() - started) < timeoutMs);

    _modelCommandSlot.abandon();
    if (_modelCommandSlot.poll(result)) return CsiModelRequestStatus::COMPLETED;
    return CsiModelRequestStatus::TIMED_OUT;
}

// P1.5: land an imported model as CANDIDATE only. A fresh generation makes it
// newer than active (apply_required), and finalizeCandidate re-seals+validates,
// so a malformed import fails here and never reaches the active slot.
CsiModelOp CSIService::importCandidateModel(const CsiSiteModel& src) {
    CsiSiteModel cand = src;
    cand.generation = _modelMgr.nextGeneration();
    uint32_t ep = (uint32_t)time(nullptr);
    if (ep > 1600000000u) cand.createdAt = ep;   // stamp import time when clock is synced
    CsiModelOp r = _modelMgr.finalizeCandidate(cand);
    if (r == CsiModelOp::OK) {
        _scheduleModelStatePublish(ModelPublishEvent::CANDIDATE_IMPORTED);
        Serial.printf("[CSI] Imported candidate gen=%lu thr=%.5f (apply required)\n",
                      (unsigned long)_modelMgr.candidate().generation, _modelMgr.candidate().threshold);
    }
    return r;
}
