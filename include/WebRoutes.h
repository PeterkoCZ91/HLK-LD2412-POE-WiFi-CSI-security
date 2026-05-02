#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H

#ifndef LITE_BUILD

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

// Forward declarations
class LD2412Service;
class MQTTService;
class SecurityMonitor;
class TelegramService;
class NotificationService;
class LogService;
class EventLog;
class BluetoothService;
class UpdateService;
class ConfigManager;
class ConfigSnapshot;
class CSIService;
struct SystemConfig;

/**
 * @brief Web Routes module for HTTP API endpoints.
 *
 * Extracts HTTP handlers from main.cpp to improve code organization.
 * All routes require authentication via checkAuth().
 */
namespace WebRoutes {

    /**
     * @brief Dependencies required by web routes
     */
    struct Dependencies {
        AsyncWebServer* server;
        AsyncEventSource* events;
        Preferences* preferences;
        LD2412Service* radar;
        MQTTService* mqttService;
        SecurityMonitor* securityMonitor;
        TelegramService* telegramBot;
        NotificationService* notificationService;
        LogService* systemLog;
        EventLog* eventLog;
        BluetoothService* bluetooth;

        // Global config
        struct SystemConfig* config;
        ConfigManager* configManager;

        String* zonesJson;
        const char* fwVersion;
        volatile bool* shouldReboot;
        
        // Zone update thread safety (TASK-011)
        String* pendingZonesJson;
        volatile bool* pendingZonesUpdate;
        SemaphoreHandle_t* zonesMutex;
        ConfigSnapshot* configSnapshot;

        // WiFi CSI service (nullptr when firmware compiled without -D USE_CSI=1)
        CSIService* csiService;
    };

    /**
     * @brief Authentication helper — checks HTTP auth, challenges with Digest on failure
     * @param request The incoming HTTP request
     * @return true if authenticated, false otherwise (sends 401 + Digest challenge)
     */
    bool checkAuth(AsyncWebServerRequest *request);

    /**
     * @brief rc4: Authentication helper — challenges with Basic on failure.
     * Used by heavy JSON endpoints to avoid Digest nonce/body-hash state under
     * AsyncTCP write-buffer pressure (see definition for full rationale).
     * @param request The incoming HTTP request
     * @return true if authenticated, false otherwise (sends 401 + Basic challenge)
     */
    bool checkAuthBasic(AsyncWebServerRequest *request);

    /**
     * @brief Initialize all web routes
     * @param deps Dependencies structure with all required pointers
     */
    void setup(Dependencies& deps);

    /**
     * @brief Setup telemetry routes (/api/telemetry, /api/health)
     */
    void setupTelemetryRoutes();

    /**
     * @brief Setup configuration routes (/api/config, /api/mqtt/config, etc.)
     */
    void setupConfigRoutes();

    /**
     * @brief Setup security routes (/api/alarm/*, /api/security/*)
     */
    void setupSecurityRoutes();

    /**
     * @brief Setup system routes (/api/restart, /api/update, /api/logs, etc.)
     */
    void setupSystemRoutes();

    /**
     * @brief Setup alarm routes (/api/alarm/*)
     */
    void setupAlarmRoutes();

    /**
     * @brief Setup log and event routes (/api/logs, /api/events)
     */
    void setupLogRoutes();

    /**
     * @brief Setup config snapshot routes (/api/config/snapshots, /api/config/restore)
     */
    void setupSnapshotRoutes();

    /**
     * @brief Setup web asset routes (/api/www/info, /api/www/upload)
     */
    void setupWwwRoutes();

    /**
     * @brief Setup WiFi CSI routes (/api/csi GET/POST/actions). No-op if !USE_CSI.
     */
    void setupCSIRoutes();

} // namespace WebRoutes

#endif // LITE_BUILD
#endif // WEB_ROUTES_H
