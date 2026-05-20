#include "CpapWebServer.h"
#include "Logger.h"
#include "UploadFSM.h"
#include "version.h"
#include "web_ui.h"
#include "WebStatus.h"
#include <time.h>

// Global trigger flags
volatile bool g_triggerUploadFlag = false;
volatile bool g_resetStateFlag = false;
volatile bool g_softRebootFlag = false;

// Monitoring trigger flags
volatile bool g_monitorActivityFlag = false;
volatile bool g_stopMonitorFlag = false;

// External FSM state (defined in main.cpp)
extern UploadState currentState;
extern unsigned long stateEnteredAt;
extern unsigned long cooldownStartedAt;
extern volatile bool uploadTaskRunning;

namespace {
static constexpr unsigned long kUploadLogsMinRefreshMs = 3000;
static constexpr size_t kUploadLogsTailBytes = 2048;
static constexpr unsigned long kUploadUiMinIntervalMs = 400;
static constexpr unsigned long kUploadNotFoundMinIntervalMs = 250;

enum UploadUiSlot : uint8_t {
    kUploadUiSlotRoot = 0,
    kUploadUiSlotLogs,
    kUploadUiSlotConfig,
    kUploadUiSlotStatus,
    kUploadUiSlotMonitor,
    kUploadUiSlotNotFound,
    kUploadUiSlotCount
};

bool isUploadUiRateLimited(UploadUiSlot slot, bool uploadInProgress, unsigned long minIntervalMs) {
    static unsigned long lastServedMs[kUploadUiSlotCount] = {0};

    if (!uploadInProgress) {
        lastServedMs[slot] = 0;
        return false;
    }

    const unsigned long now = millis();
    if (lastServedMs[slot] != 0 && (now - lastServedMs[slot]) < minIntervalMs) {
        return true;
    }

    lastServedMs[slot] = now;
    return false;
}

void sendUploadRateLimitResponse(WebServer* server,
                                 const char* contentType = "text/plain",
                                 const char* body = "Busy during upload; slow down refresh") {
    if (!server) {
        return;
    }
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Connection", "close");
    server->sendHeader("Retry-After", "1");
    server->send(429, contentType, body);
    // Proactively release socket resources under upload-time pressure.
    server->client().stop();
}
}

// Constructor
CpapWebServer::CpapWebServer(Config* cfg, UploadStateManager* state,
                             ScheduleManager* schedule, 
                             WiFiManager* wifi, CPAPMonitor* monitor)
    : server(nullptr),
      config(cfg),
      stateManager(state),
      smbStateManager(nullptr),
      scheduleManager(schedule),
      wifiManager(wifi),
      cpapMonitor(monitor),
      trafficMonitor(nullptr),
      sdManager(nullptr)
#ifdef ENABLE_OTA_UPDATES
      , otaManager(nullptr)
#endif
{
}

// Destructor
CpapWebServer::~CpapWebServer() {
    if (server) {
        server->stop();
        delete server;
    }
}

// Initialize and start the web server
bool CpapWebServer::begin() {
    LOG("[WebServer] Initializing web server on port 80...");
    
    server = new WebServer(80);

    // Collect Host header so requests to *.local can be redirected to the device IP.
    const char* headerKeys[] = {"Host"};
    server->collectHeaders(headerKeys, 1);
    
    // Register request handlers
    server->on("/", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleRoot();
    });
    server->on("/trigger-upload", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleTriggerUpload();
    });
    
    // HTML Views
    server->on("/status", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleStatusPage();
    });
    server->on("/config", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleConfigPage();
    });
    server->on("/logs", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleLogs();
    });
    server->on("/monitor", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleMonitorPage();
    });
    
    // APIs
    server->on("/api/status", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleApiStatus();
    });
    server->on("/api/config", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleApiConfig();
    });
    server->on("/api/logs", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleApiLogs();
    });
    server->on("/api/monitor-start", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleMonitorStart();
    });
    server->on("/api/monitor-stop", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleMonitorStop();
    });
    server->on("/api/sd-activity", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleSdActivity();
    });
    server->on("/reset-state", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleResetState();
    });
    server->on("/soft-reboot", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleSoftReboot();
    });
    server->on("/api/config-raw",  HTTP_GET,  [this]() { this->handleApiConfigRawGet(); });
    server->on("/api/config-raw",  HTTP_POST, [this]() { this->handleApiConfigRawPost(); });
    server->on("/api/config-lock", HTTP_POST, [this]() { this->handleApiConfigLock(); });
    
#ifdef ENABLE_OTA_UPDATES
    // OTA handlers
    server->on("/ota", [this]() {
        if (this->redirectToIpIfMdnsRequest()) return;
        this->handleOTAPage();
    });
    server->on("/ota-upload", HTTP_POST, 
               [this]() { this->handleOTAUploadComplete(); },
               [this]() { this->handleOTAUpload(); });
    server->on("/ota-url", HTTP_POST, [this]() { this->handleOTAURL(); });
#endif
    
    // Handle common browser requests silently
    server->on("/favicon.ico", [this]() { 
        if (this->redirectToIpIfMdnsRequest()) return;
        // Avoid empty-content WebServer warnings and keep this path cheap.
        server->sendHeader("Connection", "close");
        server->send(404, "text/plain", "Not found");
    });
    
    server->onNotFound([this]() { this->handleNotFound(); });
    
    // Start the server
    server->begin();
    
    LOG("[WebServer] Web server started successfully");
    LOG("[WebServer] Available endpoints:");
    LOG("[WebServer]   GET  /                  - Dashboard (HTML SPA)");
    LOG("[WebServer]   GET  /trigger-upload    - Force immediate upload");
    LOG("[WebServer]   GET  /status            - Status page (HTML)");
    LOG("[WebServer]   GET  /config            - Config page (HTML)");
    LOG("[WebServer]   GET  /logs              - Logs page (HTML)");
    LOG("[WebServer]   GET  /monitor           - SD Activity Monitor page (HTML)");
    LOG("[WebServer]   GET  /api/status        - Status information (JSON)");
    LOG("[WebServer]   GET  /api/config        - Configuration snapshot (JSON)");
    LOG("[WebServer]   GET  /api/logs          - System logs (text/plain)");
    LOG("[WebServer]   GET  /api/config-raw    - Raw config.txt content (text/plain)");
    LOG("[WebServer]   POST /api/config-raw    - Update config.txt (text/plain body)");
    LOG("[WebServer]   POST /api/config-lock   - Acquire/release config edit lock (JSON)");
    LOG("[WebServer]   GET  /api/monitor-start - Start SD activity monitoring");
    LOG("[WebServer]   GET  /api/monitor-stop  - Stop SD activity monitoring");
    LOG("[WebServer]   GET  /api/sd-activity   - SD activity data (JSON)");
    LOG("[WebServer]   GET  /reset-state       - Clear upload state");
    LOG("[WebServer]   GET  /soft-reboot       - Reboot device (skip cold-boot delays)");
#ifdef ENABLE_OTA_UPDATES
    LOG("[WebServer]   GET  /ota               - OTA update page (HTML)");
    LOG("[WebServer]   POST /ota-upload        - Upload firmware file");
    LOG("[WebServer]   POST /ota-url           - Download firmware from URL");
#endif
    
    return true;
}

// Process incoming HTTP requests
void CpapWebServer::handleClient() {
    if (server) {
        // Always service sockets to avoid stale descriptors and connection backlog.
        server->handleClient();
    }
}

bool CpapWebServer::isUploadInProgress() const {
    return uploadTaskRunning;
}

bool CpapWebServer::redirectToIpIfMdnsRequest() {
    if (!server || server->method() != HTTP_GET) {
        return false;
    }

    String host = server->header("Host");
    if (host.isEmpty()) {
        return false;
    }

    host.trim();
    int colonPos = host.indexOf(':');
    if (colonPos > 0) {
        host = host.substring(0, colonPos);
    }
    host.toLowerCase();

    if (!host.endsWith(".local")) {
        return false;
    }

    if (!wifiManager || !wifiManager->isConnected()) {
        return false;
    }

    String ip = wifiManager->getIPAddress();
    if (ip.isEmpty() || ip == "Not connected") {
        return false;
    }

    String uri = server->uri();
    if (uri.isEmpty()) {
        uri = "/";
    }

    String location = "http://" + ip + uri;
    LOG_DEBUGF("[WebServer] Redirecting mDNS request %s -> %s", host.c_str(), location.c_str());

    server->sendHeader("Location", location, true);
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Connection", "close");
    server->send(302, "text/plain", "Redirecting to device IP");
    return true;
}

// GET / - Serve static SPA from PROGMEM. Zero heap allocation.
void CpapWebServer::handleRoot() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Connection", "close");
    server->send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}

// GET /trigger-upload - Force immediate upload
void CpapWebServer::handleTriggerUpload() {
    LOG("[WebServer] Upload trigger requested via web interface");

    // Scheduled mode: reject trigger outside the upload window.
    // Triggering outside the window would acquire the SD card (CPAP may be writing),
    // do nothing (canUploadFreshData/canUploadOldData both false), then reboot — harmful.
    if (scheduleManager && !scheduleManager->isSmartMode()) {
        if (!scheduleManager->isInUploadWindow()) {
            addCorsHeaders(server);
            int startHour = scheduleManager->getUploadStartHour();
            int endHour   = scheduleManager->getUploadEndHour();
            char json[256];
            snprintf(json, sizeof(json),
                "{\"status\":\"scheduled\",\"message\":"
                "\"Scheduled mode: uploads only between %02d:00 and %02d:00. "
                "Switch to Smart mode for anytime uploads.\"}",
                startHour, endHour);
            server->send(200, "application/json", json);
            return;
        }
    }

    // Set global trigger flag — FSM picks this up in the next loop iteration
    g_triggerUploadFlag = true;

    addCorsHeaders(server);
    server->send(200, "application/json",
        "{\"status\":\"success\",\"message\":\"Upload triggered. Check serial output for progress.\"}");
}

// GET /status - JSON status information (Legacy - Removed, use handleApiStatus)


// GET /soft-reboot - Reboot immediately, skipping cold-boot delays
void CpapWebServer::handleSoftReboot() {
    LOG("[WebServer] Soft reboot requested via web interface");
    addCorsHeaders(server);
    server->send(200, "application/json",
        "{\"status\":\"success\",\"message\":\"Rebooting now (waits skipped)...\"}");
    g_softRebootFlag = true;
}

// GET /reset-state - Clear upload state
void CpapWebServer::handleResetState() {
    LOG("[WebServer] State reset requested via web interface");
    
    // Set global reset flag
    g_resetStateFlag = true;
    
    // Add CORS headers
    addCorsHeaders(server);
    
    String response = "{\"status\":\"success\",\"message\":\"Upload state will be reset. Check serial output for confirmation.\"}";
    server->send(200, "application/json", response);
}

// GET /api/config - serve pre-built static config snapshot. Zero heap allocation.
void CpapWebServer::handleApiConfig() {
    addCorsHeaders(server);
    server->send(200, "application/json", g_webConfigBuf);
}

// Handle 404 errors
void CpapWebServer::handleNotFound() {
    String uri = server->uri();

    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Connection", "close");
    
    // Silently handle common browser requests that we don't care about
    if (uri == "/favicon.ico" || uri == "/apple-touch-icon.png" || 
        uri == "/apple-touch-icon-precomposed.png" || uri == "/robots.txt") {
        server->send(404, "text/plain", "Not found");
        if (isUploadInProgress()) {
            server->client().stop();
        }
        return;
    }

    // During uploads keep 404 handling allocation-free and rate-limited.
    if (isUploadUiRateLimited(kUploadUiSlotNotFound, isUploadInProgress(), kUploadNotFoundMinIntervalMs)) {
        server->send(404, "text/plain", "Not found");
        server->client().stop();
        return;
    }
    if (isUploadInProgress()) {
        server->send(404, "text/plain", "Not found");
        server->client().stop();
        return;
    }
    
    // Log unexpected 404s
    LOG_DEBUGF("[WebServer] 404 Not Found: %s", uri.c_str());
    
    String message = "{\"status\":\"error\",\"message\":\"Endpoint not found\",\"path\":\"" + uri + "\"}";
    server->send(404, "application/json", message);
}

// Static helper: Add CORS headers to response
void CpapWebServer::addCorsHeaders(::WebServer* server) {
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Connection", "close");
}

// Helper: Get uptime as formatted string
String CpapWebServer::getUptimeString() {
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    String uptime = "";
    if (days > 0) {
        uptime += String(days) + "d ";
    }
    uptime += String(hours % 24) + "h ";
    uptime += String(minutes % 60) + "m ";
    uptime += String(seconds % 60) + "s";
    
    return uptime;
}

// Helper: Get current time as formatted string
String CpapWebServer::getCurrentTimeString() {
    time_t now;
    time(&now);
    
    if (now < 1000000000) {
        return "Not synchronized";
    }
    
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    return String(buffer);
}

// Helper: Get count of pending files (estimate)
int CpapWebServer::getPendingFilesCount() {
    if (!stateManager) {
        return 0;
    }
    
    // Count files in incomplete DATALOG folders
    // Note: This requires SD card access, which we don't have here
    // Return -1 to indicate "unknown" rather than misleading 0
    return -1;
}

// Helper: Get count of pending DATALOG folders
int CpapWebServer::getPendingFoldersCount() {
    if (!stateManager) {
        return 0;
    }
    
    return stateManager->getPendingFoldersCount();
}

// Helper class to adapt WebServer for chunked streaming via Print interface
class ChunkedPrint : public Print {
    WebServer* _server;
public:
    ChunkedPrint(WebServer* server) : _server(server) {}
    
    size_t write(uint8_t c) override {
        return write(&c, 1);
    }
    
    size_t write(const uint8_t *buffer, size_t size) override {
        if (size == 0) return 0;
        
        // Manually write chunk header (size in hex + CRLF)
        WiFiClient client = _server->client();
        client.print(String(size, HEX));
        client.write("\r\n", 2);
        
        // Write chunk data
        size_t written = client.write(buffer, size);
        
        // Write chunk footer (CRLF)
        client.write("\r\n", 2);
        
        return written;
    }
};

// GET /logs - serve SPA (client-side rendering handles logs tab)
void CpapWebServer::handleLogs() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Connection", "close");
    server->send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}
// GET /api/logs - Raw logs for AJAX
void CpapWebServer::handleApiLogs() {
    // Add CORS headers
    addCorsHeaders(server);
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    // Send headers with correct content type
    server->send(200, "text/plain; charset=utf-8", " ");
    // Stream logs directly
    ChunkedPrint chunkedOutput(server);

    if (isUploadInProgress()) {
        static unsigned long lastLogDumpMs = 0;
        const unsigned long now = millis();
        if (lastLogDumpMs == 0 || now - lastLogDumpMs >= kUploadLogsMinRefreshMs) {
            Logger::getInstance().printLogsTail(chunkedOutput, kUploadLogsTailBytes);
            lastLogDumpMs = now;
        } else {
            chunkedOutput.print("[INFO] Log stream throttled during active upload. Retry shortly.\n");
        }
    } else {
        Logger::getInstance().printLogs(chunkedOutput);
    }

    server->sendContent("");
}

// GET /config - serve SPA
void CpapWebServer::handleConfigPage() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Connection", "close");
    server->send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}
// GET /status - serve SPA
void CpapWebServer::handleStatusPage() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Connection", "close");
    server->send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}
// GET /api/status - rebuild snapshot on demand then serve it.
// Must NOT rely on the main-loop periodic rebuild: during blocking uploads
// the main loop is frozen, so g_webStatusBuf would be permanently stale.
// updateStatusSnapshot() is stack-only (no heap) and safe to call here.
void CpapWebServer::handleApiStatus() {
    updateStatusSnapshot();
    addCorsHeaders(server);
    server->send(200, "application/json", g_webStatusBuf);
}

// ---------------------------------------------------------------------------
// updateStatusSnapshot() — called every ~3 s from main loop.
// Assembles status JSON into g_webStatusBuf using snprintf only (no heap).
// ---------------------------------------------------------------------------
void CpapWebServer::updateStatusSnapshot() {
    char timeBuf[32] = "Not synchronized";
    time_t now; time(&now);
    if (now >= 1000000000) {
        struct tm tm; localtime_r(&now, &tm);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);
    }
    unsigned long upSec = millis() / 1000;
    unsigned long inStateSec = (millis() - stateEnteredAt) / 1000;
    const char* st = getStateName(currentState);
    int rssi = 0; bool wifiConn = false;
    char wifiIp[20] = "";
    if (wifiManager && wifiManager->isConnected()) {
        wifiConn = true;
        rssi = wifiManager->getSignalStrength();
        strncpy(wifiIp, wifiManager->getIPAddress().c_str(), sizeof(wifiIp) - 1);
    }
    // Active backend folder counts from the current session's state manager.
    // Pending (empty) folders are excluded from the total so the progress bar
    // only measures real data folders.  They are reported separately as a note.
    int foldersDone    = 0;
    int foldersTotal   = 0;
    int foldersPending = 0;
    if (stateManager) {
        foldersDone    = stateManager->getCompletedFoldersCount();
        foldersPending = stateManager->getPendingFoldersCount();
        foldersTotal   = foldersDone + stateManager->getIncompleteFoldersCount();
    }
    long nextUp = -1; bool timeSynced = false;
    if (scheduleManager) {
        nextUp = scheduleManager->getSecondsUntilNextUpload();
        timeSynced = scheduleManager->isTimeSynced();
    }
    // Live per-file progress from the upload task
    char liveFolder[33] = "";
    int  liveUp = 0, liveTotal = 0; bool liveActive = false;
    if (g_activeBackendStatus.valid &&
        strncmp(g_activeBackendStatus.name, "SMB", 3) == 0 &&
        g_smbSessionStatus.uploadActive) {
        strncpy(liveFolder, (const char*)g_smbSessionStatus.currentFolder, sizeof(liveFolder) - 1);
        liveUp = g_smbSessionStatus.filesUploaded; liveTotal = g_smbSessionStatus.filesTotal;
        liveActive = true;
    } else if (g_activeBackendStatus.valid &&
               strncmp(g_activeBackendStatus.name, "CLOUD", 5) == 0 &&
               g_cloudSessionStatus.uploadActive) {
        strncpy(liveFolder, (const char*)g_cloudSessionStatus.currentFolder, sizeof(liveFolder) - 1);
        liveUp = g_cloudSessionStatus.filesUploaded; liveTotal = g_cloudSessionStatus.filesTotal;
        liveActive = true;
    }

    char buf[WEB_STATUS_BUF_SIZE];
    int n = snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"in_state_sec\":%lu,\"uptime\":%lu"
        ",\"time\":\"%s\",\"time_synced\":%s"
        ",\"free_heap\":%u,\"max_alloc\":%u"
        ",\"wifi\":%s,\"rssi\":%d,\"wifi_ip\":\"%s\""
        ",\"active_backend\":\"%s\",\"folders_done\":%d,\"folders_total\":%d,\"folders_pending\":%d"
        ",\"next_backend\":\"%s\",\"next_done\":%d,\"next_total\":%d,\"next_empty\":%d,\"next_ts\":%lu"
        ",\"next_upload\":%ld"
        ",\"live_active\":%s,\"live_folder\":\"%s\",\"live_up\":%d,\"live_total\":%d"
        ",\"firmware\":\"%s\"}",
        st, inStateSec, upSec,
        timeBuf, timeSynced ? "true" : "false",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
        wifiConn ? "true" : "false", rssi, wifiIp,
        g_activeBackendStatus.name,   foldersDone, foldersTotal, foldersPending,
        g_inactiveBackendStatus.name, g_inactiveBackendStatus.foldersDone,
        g_inactiveBackendStatus.foldersTotal, g_inactiveBackendStatus.foldersEmpty,
        (unsigned long)g_inactiveBackendStatus.sessionStartTs,
        nextUp,
        liveActive ? "true" : "false", liveFolder, liveUp, liveTotal,
        FIRMWARE_VERSION);
    if (n > 0 && n < (int)sizeof(buf)) {
        memcpy(g_webStatusBuf, buf, n + 1);
    }
}

// ---------------------------------------------------------------------------
// initConfigSnapshot() — called once at boot after Config is loaded.
// ---------------------------------------------------------------------------
void CpapWebServer::initConfigSnapshot() {
    if (!config) return;
    char buf[WEB_CONFIG_BUF_SIZE];
    bool hasCloud = config->hasCloudEndpoint();
    int n = snprintf(buf, sizeof(buf),
        "{\"wifi_ssid\":\"%s\",\"hostname\":\"%s\""
        ",\"endpoint_type\":\"%s\",\"endpoint_user\":\"%s\""
        ",\"upload_mode\":\"%s\""
        ",\"upload_start_hour\":%d,\"upload_end_hour\":%d"
        ",\"inactivity_seconds\":%d"
        ",\"exclusive_access_minutes\":%d,\"cooldown_minutes\":%d"
        ",\"gmt_offset_hours\":%d,\"max_days\":%d"
        ",\"cloud_configured\":%s"
        ",\"firmware\":\"%s\""
        ",\"sleep_lab_domain\":\"%s\",\"sleep_lab_user_id\":\"%s\""
        ",\"generic_webhook_url\":\"%s\"}",
        config->getWifiSSID().c_str(),
        config->getHostname().c_str(),
        config->getEndpointType().c_str(),
        config->getEndpointUser().c_str(),
        config->getUploadMode().c_str(),
        config->getUploadStartHour(), config->getUploadEndHour(),
        config->getInactivitySeconds(),
        config->getExclusiveAccessMinutes(), config->getCooldownMinutes(),
        config->getGmtOffsetHours(), config->getMaxDays(),
        hasCloud ? "true" : "false",
        FIRMWARE_VERSION,
        config->getSleepLabDomain().c_str(),
        config->getSleepLabUserId().c_str(),
        config->getGenericWebhookUrl().c_str());
    if (n > 0 && n < (int)sizeof(buf)) {
        memcpy(g_webConfigBuf, buf, n + 1);
    }
}

// Update manager references (needed after uploader recreation)
void CpapWebServer::updateManagers(UploadStateManager* state, ScheduleManager* schedule) {
    stateManager = state;
    scheduleManager = schedule;
}

void CpapWebServer::setSmbStateManager(UploadStateManager* sm) { smbStateManager = sm; }

void CpapWebServer::setSdManager(SDCardManager* sd) { sdManager = sd; }

// ---------------------------------------------------------------------------
// GET /api/config-raw — return raw contents of /config.txt as text/plain
// ---------------------------------------------------------------------------
void CpapWebServer::handleApiConfigRawGet() {
    addCorsHeaders(server);
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server->sendHeader("Connection", "close");

    if (!sdManager) {
        server->send(503, "application/json", "{\"error\":\"SD manager unavailable\"}");
        return;
    }

    // Borrow SD control if not already held (e.g. CPAP has card, no upload running)
    bool tookControl = false;
    if (!sdManager->hasControl()) {
        if (!sdManager->takeControl()) {
            server->send(503, "application/json", "{\"error\":\"SD unavailable — CPAP may be using it\"}");
            return;
        }
        tookControl = true;
    }

    fs::FS& sd = sdManager->getFS();
    File f = sd.open("/config.txt", FILE_READ);
    if (!f) {
        if (tookControl) sdManager->releaseControl();
        server->send(404, "application/json", "{\"error\":\"config.txt not found\"}");
        return;
    }
    size_t sz = f.size();
    server->setContentLength(sz);
    server->send(200, "text/plain", "");
    // Stream in small chunks — no large heap allocation
    static char chunk[256];
    while (f.available()) {
        int n = f.readBytes(chunk, sizeof(chunk));
        if (n > 0) server->sendContent(chunk, n);
    }
    f.close();
    if (tookControl) sdManager->releaseControl();
}

// ---------------------------------------------------------------------------
// POST /api/config-raw — write new config.txt content (max 4096 bytes)
// Body: raw text/plain  |  Returns: {"ok":true} or {"error":"..."}
// ---------------------------------------------------------------------------
static constexpr size_t CONFIG_RAW_MAX_BYTES = 4096;

void CpapWebServer::handleApiConfigRawPost() {
    addCorsHeaders(server);
    server->sendHeader("Cache-Control", "no-store");
    server->sendHeader("Connection", "close");

    if (!sdManager) {
        server->send(503, "application/json", "{\"error\":\"SD manager unavailable\"}");
        return;
    }
    if (isUploadInProgress()) {
        server->send(409, "application/json", "{\"error\":\"Upload in progress — cannot edit config now\"}");
        return;
    }

    size_t bodyLen = server->arg("plain").length();
    if (bodyLen == 0) {
        server->send(400, "application/json", "{\"error\":\"Empty body\"}");
        return;
    }
    if (bodyLen > CONFIG_RAW_MAX_BYTES) {
        server->send(413, "application/json", "{\"error\":\"Content too large (max 4096 bytes)\"}");
        return;
    }

    // Borrow SD control
    bool tookControl = false;
    if (!sdManager->hasControl()) {
        if (!sdManager->takeControl()) {
            server->send(503, "application/json", "{\"error\":\"SD unavailable — CPAP may be using it\"}");
            return;
        }
        tookControl = true;
    }

    const String& body = server->arg("plain");
    fs::FS& sd = sdManager->getFS();

    // Write atomically: write to temp file, then rename
    sd.remove("/config.txt.tmp");
    File f = sd.open("/config.txt.tmp", FILE_WRITE);
    if (!f) {
        if (tookControl) sdManager->releaseControl();
        server->send(500, "application/json", "{\"error\":\"Failed to open temp file for writing\"}");
        return;
    }
    size_t written = f.print(body);
    f.close();

    if (written != bodyLen) {
        sd.remove("/config.txt.tmp");
        if (tookControl) sdManager->releaseControl();
        server->send(500, "application/json", "{\"error\":\"Write incomplete\"}");
        return;
    }

    sd.remove("/config.txt");
    if (!sd.rename("/config.txt.tmp", "/config.txt")) {
        if (tookControl) sdManager->releaseControl();
        server->send(500, "application/json", "{\"error\":\"Rename failed\"}");
        return;
    }

    if (tookControl) sdManager->releaseControl();
    LOG("[WebServer] config.txt updated via web UI");
    server->send(200, "application/json", "{\"ok\":true,\"message\":\"Saved. Reboot to apply.\"}");
}

// ---------------------------------------------------------------------------
// POST /api/config-lock — acquire or release the config edit lock
// Body: {"lock":true} or {"lock":false}
// While held, the FSM will not start a new upload session.
// Auto-expires after 30 minutes (CONFIG_EDIT_LOCK_TIMEOUT_MS in main.cpp).
// ---------------------------------------------------------------------------
void CpapWebServer::handleApiConfigLock() {
    addCorsHeaders(server);
    server->sendHeader("Cache-Control", "no-store");
    server->sendHeader("Connection", "close");

    String body = server->arg("plain");
    bool lock;
    if (body.indexOf("true") >= 0) {
        lock = true;
    } else if (body.indexOf("false") >= 0) {
        lock = false;
    } else {
        server->send(400, "application/json", "{\"error\":\"Body must contain 'true' or 'false'\"}");
        return;
    }

    if (lock && isUploadInProgress()) {
        server->send(409, "application/json",
                     "{\"error\":\"Upload in progress — cannot lock config now\",\"locked\":false}");
        return;
    }

    g_configEditLock = lock;
    if (lock) {
        g_configEditLockAt = millis();
        LOG("[WebServer] Config edit lock ACQUIRED — upload FSM paused");
        server->send(200, "application/json", "{\"ok\":true,\"locked\":true}");
    } else {
        g_configEditLockAt = 0;
        LOG("[WebServer] Config edit lock RELEASED — upload FSM resumed");
        server->send(200, "application/json", "{\"ok\":true,\"locked\":false}");
    }
}

// Set WiFi manager reference
void CpapWebServer::setWiFiManager(WiFiManager* wifi) {
    wifiManager = wifi;
}

// Set TrafficMonitor reference
void CpapWebServer::setTrafficMonitor(TrafficMonitor* tm) {
    trafficMonitor = tm;
}

// Helper: Escape special characters for JSON string
String CpapWebServer::escapeJson(const String& str) {
    String escaped = "";
    escaped.reserve(str.length() + 20);  // Reserve extra space for escape sequences
    
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        switch (c) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                // Handle control characters (0x00-0x1F)
                if (c >= 0 && c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    escaped += buf;
                } else {
                    escaped += c;
                }
                break;
        }
    }
    
    return escaped;
}

#ifdef ENABLE_OTA_UPDATES
// Set OTA manager reference
void CpapWebServer::setOTAManager(OTAManager* ota) {
    otaManager = ota;
}

// GET /ota - OTA update page
void CpapWebServer::handleOTAPage() {
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);

    auto sendChunk = [this](const String& s) {
        server->sendContent(s);
    };

    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Firmware Update - CPAP Auto-Uploader</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "*{box-sizing:border-box;margin:0;padding:0}";
    html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
    html += "background:#0f1923;color:#c7d5e0;min-height:100vh;padding:20px}";
    html += ".wrap{max-width:900px;margin:0 auto}";
    html += "h1{font-size:1.6em;color:#fff;margin-bottom:4px}";
    html += ".subtitle{color:#66c0f4;font-size:0.9em;margin-bottom:20px}";
    html += ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;margin-bottom:20px}";
    html += ".card{background:#1b2838;border:1px solid #2a475e;border-radius:10px;padding:18px}";
    html += ".card h2{font-size:0.85em;text-transform:uppercase;letter-spacing:1px;color:#66c0f4;margin-bottom:12px;border-bottom:1px solid #2a475e;padding-bottom:8px}";
    html += ".row{display:flex;justify-content:space-between;padding:5px 0;font-size:0.88em}";
    html += ".row .k{color:#8f98a0}.row .v{color:#c7d5e0;font-weight:500;text-align:right}";
    html += ".btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:10px 18px;border-radius:6px;font-size:0.85em;font-weight:600;text-decoration:none;border:none;cursor:pointer;transition:all 0.2s;width:100%}";
    html += ".btn-primary{background:#66c0f4;color:#0f1923}.btn-primary:hover{background:#88d0ff}";
    html += ".btn-secondary{background:#2a475e;color:#c7d5e0;width:auto}.btn-secondary:hover{background:#3a5a7e}";
    html += ".btn-danger{background:#c0392b;color:#fff}.btn-danger:hover{background:#e04030}";
    html += ".alert{background:#3a2a1a;border:1px solid #aa6622;border-radius:8px;padding:14px;margin-bottom:16px}";
    html += ".alert h3{color:#ffaa44;font-size:0.9em;margin-bottom:6px}";
    html += ".alert ul{padding-left:20px;color:#c7d5e0;font-size:0.85em}";
    html += ".alert li{margin-bottom:4px}";
    html += ".form-group{margin-bottom:15px}";
    html += ".form-group label{display:block;margin-bottom:6px;color:#8f98a0;font-size:0.9em}";
    html += ".form-group input{width:100%;padding:10px;background:#0f1923;border:1px solid #2a475e;color:#fff;border-radius:6px;font-size:0.9em}";
    html += ".form-group input:focus{outline:none;border-color:#66c0f4}";
    html += ".status-msg{margin-top:10px;font-size:0.9em;min-height:1.2em}";
    html += ".status-msg.success{color:#44ff44}";
    html += ".status-msg.error{color:#ff4444}";
    html += ".status-msg.info{color:#66c0f4}";
    html += ".actions{margin-top:20px}";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='wrap'>";
    
    server->send(200, "text/html; charset=utf-8", html);

    html = "<h1>Firmware Update</h1>";
    html += "<p class='subtitle'>System Maintenance</p>";
    sendChunk(html);
    
    html = "<div class='cards'>";
    
    // Current version info
    html += "<div class='card'>";
    html += "<h2>Current Status</h2>";
    if (otaManager) {
        html += "<div class='row'><span class='k'>Current Version</span><span class='v'>" + otaManager->getCurrentVersion() + "</span></div>";
    } else {
        html += "<div class='row'><span class='k'>Current Version</span><span class='v'>Unknown</span></div>";
    }
    html += "</div>";
    
    // Warning message
    html += "<div class='card' style='grid-column:1/-1'>";
    html += "<h2>Important Safety Information</h2>";
    html += "<div class='alert'>";
    html += "<h3>WARNING</h3>";
    html += "<ul>";
    html += "<li><strong>Do not power off</strong> the device during update</li>";
    html += "<li><strong>Ensure stable WiFi</strong> connection before starting</li>";
    html += "<li><strong>Do NOT remove SD card</strong> from CPAP machine during update</li>";
    html += "<li>Update process takes 1-2 minutes</li>";
    html += "<li>Device will restart automatically when complete</li>";
    html += "</ul>";
    html += "</div>";
    html += "</div>";
    
    // Check if update is in progress
    if (otaManager && otaManager->isUpdateInProgress()) {
        html += "<div class='card' style='grid-column:1/-1'>";
        html += "<h2>Update In Progress</h2>";
        html += "<p style='color:#ffaa44'>A firmware update is currently in progress. Please wait for it to complete.</p>";
        html += "</div>";
    } else {
        // File upload method
        html += "<div class='card'>";
        html += "<h2>Method 1: File Upload</h2>";
        html += "<form id='uploadForm' enctype='multipart/form-data'>";
        html += "<div class='form-group'>";
        html += "<label for='firmwareFile'>Select firmware file (.bin):</label>";
        html += "<input type='file' id='firmwareFile' name='firmware' accept='.bin' required>";
        html += "</div>";
        html += "<button type='submit' class='btn btn-primary'>Upload & Install</button>";
        html += "<div id='uploadStatus' class='status-msg'></div>";
        html += "</form>";
        html += "</div>";
        
        // URL download method
        html += "<div class='card'>";
        html += "<h2>Method 2: URL Download</h2>";
        html += "<form id='urlForm'>";
        html += "<div class='form-group'>";
        html += "<label for='firmwareURL'>Firmware URL:</label>";
        html += "<input type='url' id='firmwareURL' name='url' placeholder='https://github.com/.../firmware.bin' required>";
        html += "</div>";
        html += "<button type='submit' class='btn btn-primary'>Download & Install</button>";
        html += "<div id='downloadStatus' class='status-msg'></div>";
        html += "</form>";
        html += "</div>";
    }
    
    html += "</div>"; // end cards
    
    html += "<div class='actions'>";
    html += "<a href='/' class='btn btn-secondary'>&larr; Back to Status</a>";
    html += "</div>";
    
    // JavaScript for handling uploads
    html += "<script>";
    html += "let updateInProgress = false;";
    
    // File upload handler
    html += "document.getElementById('uploadForm')?.addEventListener('submit', function(e) {";
    html += "  e.preventDefault();";
    html += "  if (updateInProgress) return;";
    html += "  const fileInput = document.getElementById('firmwareFile');";
    html += "  if (!fileInput.files[0]) { alert('Please select a file'); return; }";
    html += "  uploadFirmware(fileInput.files[0]);";
    html += "});";
    
    // URL download handler
    html += "document.getElementById('urlForm')?.addEventListener('submit', function(e) {";
    html += "  e.preventDefault();";
    html += "  if (updateInProgress) return;";
    html += "  const url = document.getElementById('firmwareURL').value;";
    html += "  if (!url) { alert('Please enter a URL'); return; }";
    html += "  downloadFirmware(url);";
    html += "});";
    
    // Upload function
    html += "function uploadFirmware(file) {";
    html += "  updateInProgress = true;";
    html += "  setStatus('uploadStatus', 'info', 'Uploading firmware... 0%');";
    html += "  const formData = new FormData();";
    html += "  formData.append('firmware', file);";
    html += "  const xhr = new XMLHttpRequest();";
    html += "  xhr.upload.addEventListener('progress', function(e) {";
    html += "    if (e.lengthComputable) {";
    html += "      const percent = Math.round((e.loaded / e.total) * 100);";
    html += "      setStatus('uploadStatus', 'info', 'Uploading firmware... ' + percent + '%');";
    html += "    }";
    html += "  });";
    html += "  xhr.addEventListener('load', function() {";
    html += "    try {";
    html += "      const data = JSON.parse(xhr.responseText);";
    html += "      handleResult(data, 'uploadStatus');";
    html += "    } catch(e) { handleResult({success:false, message:'Invalid response'}, 'uploadStatus'); }";
    html += "  });";
    html += "  xhr.addEventListener('error', function() { handleResult({success:false, message:'Network error'}, 'uploadStatus'); });";
    html += "  xhr.open('POST', '/ota-upload');";
    html += "  xhr.send(formData);";
    html += "}";
    
    // Download function
    html += "function downloadFirmware(url) {";
    html += "  updateInProgress = true;";
    html += "  setStatus('downloadStatus', 'info', 'Downloading firmware... (this may take a minute)');";
    html += "  const formData = new FormData();";
    html += "  formData.append('url', url);";
    html += "  fetch('/ota-url', { method: 'POST', body: formData })";
    html += "    .then(response => response.json())";
    html += "    .then(data => handleResult(data, 'downloadStatus'))";
    html += "    .catch(error => handleResult({success:false, message:String(error)}, 'downloadStatus'));";
    html += "}";
    
    // Result handlers
    html += "function setStatus(id, type, msg) {";
    html += "  const el = document.getElementById(id);";
    html += "  if(el) { el.className = 'status-msg ' + type; el.textContent = msg; }";
    html += "}";
    
    html += "function handleResult(data, statusId) {";
    html += "  updateInProgress = false;";
    html += "  if (data.success) {";
    html += "    setStatus(statusId, 'success', 'Success! ' + data.message);";
    html += "    let timeLeft = 30;";
    html += "    setInterval(() => {";
    html += "      timeLeft--;";
    html += "      if (timeLeft <= 0) window.location.href = '/';";
    html += "      else setStatus(statusId, 'success', 'Success! Redirecting to dashboard in ' + timeLeft + 's...');";
    html += "    }, 1000);";
    html += "  } else {";
    html += "    setStatus(statusId, 'error', 'Failed: ' + data.message);";
    html += "  }";
    html += "}";
    
    html += "</script>";
    html += "</div></body></html>";
    sendChunk(html);
    
    server->sendContent("");
}

// POST /ota-upload - Handle firmware file upload
void CpapWebServer::handleOTAUpload() {
    static bool uploadError = false;
    static bool successResponseSent = false;
    static unsigned long lastUploadAttempt = 0;
    
    LOG_DEBUG("[OTA] handleOTAUpload() called");
    
    if (!otaManager) {
        LOG_ERROR("[OTA] OTA manager not initialized");
        server->send(500, "application/json", "{\"success\":false,\"message\":\"OTA manager not initialized\"}");
        return;
    }
    
    HTTPUpload& upload = server->upload();
    LOG_DEBUGF("[OTA] Upload status: %d", upload.status);
    
    if (upload.status == UPLOAD_FILE_START) {
        LOG_DEBUGF("[OTA] UPLOAD_FILE_START - filename: %s, totalSize: %u", upload.filename.c_str(), upload.totalSize);
        
        // Only check for "already in progress" during START phase
        if (otaManager->isUpdateInProgress()) {
            LOG_ERROR("[OTA] Update already in progress");
            server->send(400, "application/json", "{\"success\":false,\"message\":\"Update already in progress\"}");
            return;
        }
        
        // Prevent rapid repeated calls (debounce)
        unsigned long now = millis();
        if (now - lastUploadAttempt < 1000) {  // 1 second debounce
            LOG_WARN("[OTA] Upload attempt too soon, ignoring");
            server->send(429, "application/json", "{\"success\":false,\"message\":\"Too many requests\"}");
            return;
        }
        lastUploadAttempt = now;
        
        uploadError = false;
        successResponseSent = false;
        
        if (upload.totalSize == 0) {
            LOG_WARN("[OTA] Total size is 0, using chunked upload mode");
        }
        
        if (!otaManager->startUpdate(upload.totalSize)) {
            LOG_ERROR("[OTA] Failed to start update");
            uploadError = true;
            return;
        }
        
        LOG_DEBUG("[OTA] Update started successfully");
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        LOG_DEBUGF("[OTA] UPLOAD_FILE_WRITE - currentSize: %u, uploadError: %s", 
                   upload.currentSize, uploadError ? "true" : "false");
        
        if (!uploadError && !otaManager->writeChunk(upload.buf, upload.currentSize)) {
            LOG_ERROR("[OTA] Failed to write chunk");
            uploadError = true;
            return;
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        LOG_DEBUGF("[OTA] UPLOAD_FILE_END - totalSize: %u, uploadError: %s", 
                   upload.totalSize, uploadError ? "true" : "false");
        
        if (uploadError) {
            LOG_ERROR("[OTA] Upload failed due to previous errors");
            otaManager->abortUpdate();
            // Don't send response here, handleOTAUploadComplete will handle it
            return;
        }
        
        if (otaManager->finishUpdate()) {
            LOG("[OTA] Update completed successfully, restarting...");
            // Send success response before restarting
            server->send(200, "application/json", "{\"success\":true,\"message\":\"Update completed! Device will restart in 3 seconds.\"}");
            successResponseSent = true;
            
            // Restart after a short delay
            delay(3000);
            ESP.restart();
        } else {
            LOG_ERROR("[OTA] Failed to finish update");
            // Don't send response here, handleOTAUploadComplete will handle it
        }
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        LOG_WARN("[OTA] UPLOAD_FILE_ABORTED");
        otaManager->abortUpdate();
        uploadError = true;
        // Don't send response here, handleOTAUploadComplete will handle it
    } else {
        LOG_DEBUGF("[OTA] Unknown upload status: %d", upload.status);
    }
}

// POST /ota-upload - Handle completion of firmware file upload
void CpapWebServer::handleOTAUploadComplete() {
    LOG_DEBUG("[OTA] handleOTAUploadComplete() called");
    
    if (!otaManager) {
        LOG_ERROR("[OTA] OTA manager not initialized");
        server->send(500, "application/json", "{\"success\":false,\"message\":\"OTA manager not initialized\"}");
        return;
    }
    
    // This is called after the upload is complete
    // The actual upload processing happens in handleOTAUpload()
    // Here we just send the final response if one hasn't been sent already
    
    // Check if there was an error during upload
    String error = otaManager->getLastError();
    if (!error.isEmpty()) {
        LOG_ERROR("[OTA] Upload completed with error");
        server->send(500, "application/json", "{\"success\":false,\"message\":\"Upload failed: " + error + "\"}");
    } else {
        // If we reach here and there's no error, it means the update was successful
        // but the device hasn't restarted yet (which shouldn't normally happen)
        LOG_DEBUG("[OTA] Upload completed successfully");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Upload completed successfully! Device will restart shortly.\"}");
    }
}

// POST /ota-url - Handle firmware download from URL
void CpapWebServer::handleOTAURL() {
    if (!otaManager) {
        server->send(500, "application/json", "{\"success\":false,\"message\":\"OTA manager not initialized\"}");
        return;
    }
    
    if (otaManager->isUpdateInProgress()) {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"Update already in progress\"}");
        return;
    }
    
    if (!server->hasArg("url")) {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"URL parameter required\"}");
        return;
    }
    
    String url = server->arg("url");
    LOG_DEBUGF("[OTA] Starting download from URL: %s", url.c_str());
    
    if (otaManager->updateFromURL(url)) {
        LOG("[OTA] Update completed successfully, restarting...");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Update completed! Device will restart in 3 seconds.\"}");
        
        // Restart after a short delay
        delay(3000);
        ESP.restart();
    } else {
        LOG_ERROR("[OTA] Failed to update from URL");
        String error = otaManager->getLastError();
        server->send(500, "application/json", "{\"success\":false,\"message\":\"Update failed: " + error + "\"}");
    }
}

#endif // ENABLE_OTA_UPDATES

// ============================================================================
// SD Activity Monitor Handlers
// ============================================================================

void CpapWebServer::handleMonitorStart() {
    addCorsHeaders(server);
    g_monitorActivityFlag = true;
    server->send(200, "application/json", "{\"success\":true,\"message\":\"Monitoring started\"}");
}

void CpapWebServer::handleMonitorStop() {
    addCorsHeaders(server);
    g_stopMonitorFlag = true;
    server->send(200, "application/json", "{\"success\":true,\"message\":\"Monitoring stopped\"}");
}

void CpapWebServer::handleSdActivity() {
    addCorsHeaders(server);
    
    if (!trafficMonitor) {
        server->send(500, "application/json", "{\"error\":\"TrafficMonitor not available\"}");
        return;
    }

    // During uploads return a compact payload with no sample history.
    // This keeps the endpoint alive for UI health checks while avoiding heavy String churn.
    if (isUploadInProgress()) {
        static unsigned long lastCompactRefreshMs = 0;
        static char compactJson[256] = {0};
        const unsigned long now = millis();
        if (compactJson[0] == '\0' || now - lastCompactRefreshMs >= 1000) {
            snprintf(compactJson,
                     sizeof(compactJson),
                     "{\"last_pulse_count\":%d,\"consecutive_idle_ms\":%lu,\"longest_idle_ms\":%lu,\"total_active_samples\":%lu,\"total_idle_samples\":%lu,\"is_busy\":%s,\"sample_count\":%d,\"samples\":[],\"degraded\":true}",
                     trafficMonitor->getLastPulseCount(),
                     (unsigned long)trafficMonitor->getConsecutiveIdleMs(),
                     (unsigned long)trafficMonitor->getLongestIdleMs(),
                     (unsigned long)trafficMonitor->getTotalActiveSamples(),
                     (unsigned long)trafficMonitor->getTotalIdleSamples(),
                     trafficMonitor->isBusy() ? "true" : "false",
                     trafficMonitor->getSampleCount());
            lastCompactRefreshMs = now;
        }

        server->send(200, "application/json", compactJson);
        return;
    }
    
    // Build JSON with current stats and recent samples
    String json = "{";
    json += "\"last_pulse_count\":" + String(trafficMonitor->getLastPulseCount()) + ",";
    json += "\"consecutive_idle_ms\":" + String(trafficMonitor->getConsecutiveIdleMs()) + ",";
    json += "\"longest_idle_ms\":" + String(trafficMonitor->getLongestIdleMs()) + ",";
    json += "\"total_active_samples\":" + String(trafficMonitor->getTotalActiveSamples()) + ",";
    json += "\"total_idle_samples\":" + String(trafficMonitor->getTotalIdleSamples()) + ",";
    json += "\"is_busy\":" + String(trafficMonitor->isBusy() ? "true" : "false") + ",";
    json += "\"sample_count\":" + String(trafficMonitor->getSampleCount()) + ",";
    
    // Include the last N samples from the circular buffer
    json += "\"samples\":[";
    int count = trafficMonitor->getSampleCount();
    int head = trafficMonitor->getSampleHead();
    int maxSamples = TrafficMonitor::MAX_SAMPLES;
    const ActivitySample* buffer = trafficMonitor->getSampleBuffer();
    
    // Output samples oldest-first (from tail to head)
    int start = (count < maxSamples) ? 0 : head;
    int outputCount = min(count, 60);  // Limit to last 60 seconds for web response
    int skip = count - outputCount;
    
    bool first = true;
    for (int i = 0; i < count; i++) {
        if (i < skip) continue;
        int idx = (start + i) % maxSamples;
        if (!first) json += ",";
        json += "{\"t\":" + String(buffer[idx].timestamp);
        json += ",\"p\":" + String(buffer[idx].pulseCount);
        json += ",\"a\":" + String(buffer[idx].active ? 1 : 0) + "}";
        first = false;
    }
    
    json += "]}";
    
    server->send(200, "application/json", json);
}

void CpapWebServer::handleMonitorPage() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Connection", "close");
    server->send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}
