#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

#include "Config.h"
#include "SDCardManager.h"
#include "WiFiManager.h"
#include "FileUploader.h"
#include "Logger.h"
#include "pins_config.h"
#include "version.h"

#include "TrafficMonitor.h"
#include "UploadFSM.h"

// True when esp_restart() was the reset cause (ESP_RST_SW).
// Any programmatic restart means the CPAP machine was already idle and
// voltages are stable, so cold-boot delays (stabilization, Smart Wait,
// NTP settle) can be skipped.  Set once in setup() from esp_reset_reason().
bool g_heapRecoveryBoot = false;

#ifdef ENABLE_OTA_UPDATES
#include "OTAManager.h"
#endif

#ifdef ENABLE_WEBSERVER
#include "CpapWebServer.h"
#include "CPAPMonitor.h"
#endif

// ============================================================================
// Global Objects
// ============================================================================
Config config;
SDCardManager sdManager;
WiFiManager wifiManager;
FileUploader* uploader = nullptr;
TrafficMonitor trafficMonitor;

#ifdef ENABLE_OTA_UPDATES
OTAManager otaManager;
#endif

#ifdef ENABLE_WEBSERVER
CpapWebServer* webServer = nullptr;
CPAPMonitor* cpapMonitor = nullptr;
#endif

// ============================================================================
// Bus yield — CPAP requested SD access while ESP owns the bus
// ============================================================================
volatile bool g_cpapYieldRequest = false;

void IRAM_ATTR cpapYieldISR() {
    g_cpapYieldRequest = true;
}

// ============================================================================
// Upload FSM State
// ============================================================================
UploadState currentState = UploadState::IDLE;
unsigned long stateEnteredAt = 0;
unsigned long cooldownStartedAt = 0;
bool uploadCycleHadTimeout = false;
bool g_nothingToUpload = false;  // Set when pre-flight finds no work — skip reboot, go to cooldown
bool g_configEditLock = false;   // Set by web UI config editor — FSM won't start new upload session
unsigned long g_configEditLockAt = 0;  // millis() when lock was acquired
const unsigned long CONFIG_EDIT_LOCK_TIMEOUT_MS = 30UL * 60UL * 1000UL;  // 30 min auto-expire

// Monitoring mode flags
bool monitoringRequested = false;
bool stopMonitoringRequested = false;

// IDLE state periodic check
unsigned long lastIdleCheck = 0;
const unsigned long IDLE_CHECK_INTERVAL_MS = 60000;  // 60 seconds

// FreeRTOS upload task (runs upload on separate core for web server responsiveness)
volatile bool uploadTaskRunning = false;
volatile bool uploadTaskComplete = false;
volatile UploadResult uploadTaskResult = UploadResult::ERROR;
TaskHandle_t uploadTaskHandle = nullptr;

// Software watchdog: upload task updates this heartbeat; main loop kills task if stale
volatile unsigned long g_uploadHeartbeat = 0;
const unsigned long UPLOAD_WATCHDOG_TIMEOUT_MS = 120000;  // 2 minutes
const uint32_t UPLOAD_ASYNC_MIN_MAX_ALLOC_BYTES = 50000;   // Below this, prefer blocking upload to preserve heap

struct UploadTaskParams {
    FileUploader* uploader;
    SDCardManager* sdManager;
    int maxMinutes;
    DataFilter filter;
};

// ============================================================================
// Global State (legacy + shared)
// ============================================================================
unsigned long lastNtpSyncAttempt = 0;
const unsigned long NTP_RETRY_INTERVAL_MS = 5 * 60 * 1000;  // 5 minutes

unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastSdCardRetry = 0;

// SD card logging periodic dump timing
unsigned long lastLogDumpTime = 0;
const unsigned long LOG_DUMP_INTERVAL_MS = 10 * 1000;  // 10 seconds

// Runtime debug mode: set from config DEBUG=true after config load.
// Gates [res fh= ma= fd=] heap suffix on all log lines and verbose pre-flight output.
bool g_debugMode = false;

#ifdef ENABLE_WEBSERVER
// External trigger flags (defined in WebServer.cpp)
extern volatile bool g_triggerUploadFlag;
extern volatile bool g_resetStateFlag;

// Monitoring trigger flags (defined in WebServer.cpp)
extern volatile bool g_monitorActivityFlag;
extern volatile bool g_stopMonitorFlag;
#endif

// ============================================================================
// FSM Helper
// ============================================================================
void transitionTo(UploadState newState) {
    LOGF("[FSM] %s -> %s", getStateName(currentState), getStateName(newState));
    currentState = newState;
    stateEnteredAt = millis();
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Convert ESP32 reset reason to human-readable string
 * Useful for diagnosing power issues, crashes, and unexpected resets
 */
const char* getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "Unknown";
        case ESP_RST_POWERON:
            return "Power-on reset (normal startup)";
        case ESP_RST_EXT:
            return "External reset via EN pin";
        case ESP_RST_SW:
            return "Software reset via esp_restart()";
        case ESP_RST_PANIC:
            return "Software panic/exception";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog timeout";
        case ESP_RST_TASK_WDT:
            return "Task watchdog timeout";
        case ESP_RST_WDT:
            return "Other watchdog timeout";
        case ESP_RST_DEEPSLEEP:
            return "Wake from deep sleep";
        case ESP_RST_BROWNOUT:
            return "Brown-out reset (low voltage)";
        case ESP_RST_SDIO:
            return "SDIO reset";
        default:
            return "Unrecognized reset reason";
    }
}

// ============================================================================
// Setup Function
// ============================================================================
void setup() {
    // Initialize serial port
    Serial.begin(115200);
    
    // CRITICAL: Immediately release SD card control to CPAP machine
    // This must happen before any delays to prevent CPAP machine errors
    // Initialize control pins
    pinMode(CS_SENSE, INPUT);
    pinMode(SD_SWITCH_PIN, OUTPUT);
    digitalWrite(SD_SWITCH_PIN, SD_SWITCH_CPAP_VALUE);
    
    delay(1000);
    LOG("\n\n=== CPAP Data Auto-Uploader ===");
    LOGF("Firmware Version: %s", FIRMWARE_VERSION);
    LOGF("Build Info: %s", BUILD_INFO);
    LOGF("Build Time: %s", FIRMWARE_BUILD_TIME);
    
    // Log reset reason for power/stability diagnostics
    esp_reset_reason_t resetReason = esp_reset_reason();
    LOG_INFOF("Reset reason: %s", getResetReasonString(resetReason));
    
    // Check for power-related issues
    if (resetReason == ESP_RST_BROWNOUT) {
        LOG_ERROR("WARNING: System reset due to brown-out (insufficient power supply), this could be caused by:");
        LOG_ERROR(" - the CPAP was disconnected from the power supply");
        LOG_ERROR(" - the card was removed");
        LOG_ERROR(" - the CPAP machine cannot provide enough power");
    } else if (resetReason == ESP_RST_PANIC) {
        LOG_WARN("System reset due to software panic - check for stability issues");
    } else if (resetReason == ESP_RST_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT) {
        LOG_WARN("System reset due to watchdog timeout - possible hang or power issue");
    }

    // Initialize SD card control
    if (!sdManager.begin()) {
        LOG("Failed to initialize SD card manager");
        return;
    }
    
    // Initialize TrafficMonitor (PCNT-based bus activity detection on CS_SENSE pin)
    trafficMonitor.begin(CS_SENSE);

    // Determine boot type: software reset (ESP_RST_SW) = soft-reboot / FastBoot.
    // Cold boots (power-on, brownout, watchdog) use distinct reason codes.
    g_heapRecoveryBoot = (esp_reset_reason() == ESP_RST_SW);
    bool fastBoot = g_heapRecoveryBoot;

    // Smart Wait constants — same values for both cold and soft-reboot.
    // 5 s of continuous SD bus silence required; give up after 45 s max.
    const unsigned long SMART_WAIT_MAX_MS      = 45000;
    const unsigned long SMART_WAIT_REQUIRED_MS =  5000;

    auto runSmartWait = [&]() {
        LOG("Checking for CPAP SD card activity (Smart Wait)...");
        unsigned long waitStart = millis();
        bool busIsQuiet = false;
        while (millis() - waitStart < SMART_WAIT_MAX_MS) {
            trafficMonitor.update();
            delay(10);
            if (trafficMonitor.isIdleFor(SMART_WAIT_REQUIRED_MS)) {
                LOGF("Smart Wait: %lums of bus silence — CPAP is idle", SMART_WAIT_REQUIRED_MS);
                busIsQuiet = true;
                break;
            }
        }
        if (!busIsQuiet) {
            LOG_WARN("Smart Wait timed out — bus still active, proceeding anyway");
        }
    };

    if (fastBoot) {
        // Soft-reboot: voltages already stable, skip 15 s electrical stabilization.
        // Smart Wait still runs — CPAP may have been mid-access when the reboot
        // was triggered and we must wait for it to finish before touching the SD card.
        LOG("[FastBoot] Software reset — skipping 15s electrical stabilization");
        runSmartWait();
    } else {
        // Cold boot: wait for power-rail stabilization and CPAP boot sequence to settle,
        // then wait for SD bus silence before attempting to take SD card control.
        LOG("Waiting 15s for electrical stabilization...");
        delay(15000);
        runSmartWait();
    }
    
    LOG("Boot delay complete, attempting SD card access...");

    // Take control of SD card
    LOG("Waiting to access SD card...");
    while (!sdManager.takeControl()) {
        delay(1000);
    }

    // Check NVS flags from previous boot
    {
        Preferences resetPrefs;
        resetPrefs.begin("cpap_flags", false);
        
        // Check if software watchdog killed the upload task last boot
        bool watchdogKill = resetPrefs.getBool("watchdog_kill", false);
        if (watchdogKill) {
            LOG_WARN("=== Previous boot: upload task was killed by software watchdog (hung >2 min) ===");
            resetPrefs.putBool("watchdog_kill", false);
        }
        
        bool resetPending = resetPrefs.getBool("reset_state", false);
        if (resetPending) {
            LOG("=== Completing deferred state reset (flag set before reboot) ===");
            resetPrefs.putBool("reset_state", false);
            resetPrefs.end();
            
            // Delete all known state file paths: per-backend (current) + old default (legacy)
            static const char* STATE_FILES[] = {
                "/.upload_state.v2.smb",
                "/.upload_state.v2.smb.log",
                "/.upload_state.v2.cloud",
                "/.upload_state.v2.cloud.log",
                "/.upload_state.v2",        // legacy: pre-split single-manager path
                "/.upload_state.v2.log",
            };
            bool removedAny = false;
            for (const char* path : STATE_FILES) {
                if (sdManager.getFS().remove(path)) {
                    LOGF("Deleted state file: %s", path);
                    removedAny = true;
                }
            }
            if (!removedAny) {
                LOG_WARN("No state files found (may already be clean)");
            }
            LOG("State reset complete — continuing with fresh start");
        } else {
            resetPrefs.end();
        }
    }

    // Read config file from SD card
    LOG("Loading configuration...");
    if (!config.loadFromSD(sdManager.getFS())) {
        LOG_ERROR("Failed to load configuration - cannot continue");
        LOG_ERROR("Please check config.txt file on SD card");
        
        sdManager.releaseControl();
        
        // Dump logs to SD card for configuration failures
        bool dumped = Logger::getInstance().dumpLogsToSDCard("config_load_failed");
        if (!dumped) {
            LOG_WARN("Failed to dump logs to SD card (config_load_failed)");
        }

        // Fail-safe: always force SD switch back to CPAP before aborting setup
        digitalWrite(SD_SWITCH_PIN, SD_SWITCH_CPAP_VALUE);
        
        return;
    }

    LOG("Configuration loaded successfully");
    g_debugMode = config.getDebugMode();
    if (g_debugMode) {
        LOG_WARN("DEBUG mode enabled — verbose pre-flight logs and heap stats active");
    }
    LOG_DEBUGF("WiFi SSID: %s", config.getWifiSSID().c_str());
    LOG_DEBUGF("Endpoint: %s", config.getEndpoint().c_str());

    // Configure SD card logging if enabled (debugging only; can block CPAP SD access)
    if (config.getLogToSdCard()) {
        LOG_WARN("Enabling SD card logging - DEBUGGING ONLY - may block CPAP SD access; use scheduled mode outside therapy times");
        LOG_WARN("Logs will be dumped every 10 seconds");
        Logger::getInstance().enableSdCardLogging(true, &sdManager.getFS());
    }

    // Release SD card back to CPAP machine
    sdManager.releaseControl();

    // Apply power management settings from config
    LOG("Applying power management settings...");
    
    // Set CPU frequency
    int targetCpuMhz = config.getCpuSpeedMhz();
    setCpuFrequencyMhz(targetCpuMhz);
    LOGF("CPU frequency set to %dMHz", getCpuFrequencyMhz());

    // Setup WiFi event handlers for logging
    wifiManager.setupEventHandlers();

    // Initialize WiFi in station mode
    if (!wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword())) {
        LOG("Failed to connect to WiFi");
        // Note: WiFiManager already dumps logs to SD card on connection failures
        return;
    }
    
    // Start mDNS responder (allows access via http://cpap.local or configured hostname)
    wifiManager.startMDNS(config.getHostname());
    
    // Apply WiFi power settings after connection is established
    wifiManager.applyPowerSettings(config.getWifiTxPower(), config.getWifiPowerSaving());
    LOG("WiFi power management settings applied");

    // Initialize uploader
    uploader = new FileUploader(&config, &wifiManager);
    
    // Take control of SD card to initialize uploader components
    LOG("Initializing uploader...");
    if (sdManager.takeControl()) {
        if (!uploader->begin(sdManager.getFS())) {
            LOG_ERROR("Failed to initialize uploader");
            sdManager.releaseControl();
            return;
        }
        sdManager.releaseControl();
        g_heapRecoveryBoot = false;  // consumed — only skip delays on this one boot
        LOG("Uploader initialized successfully");
    } else {
        LOG_ERROR("Failed to take SD card control for uploader initialization");
        return;
    }
    
#ifdef ENABLE_OTA_UPDATES
    // Initialize OTA manager
    LOG("Initializing OTA manager...");
    if (!otaManager.begin()) {
        LOG_ERROR("Failed to initialize OTA manager");
        return;
    }
    otaManager.setCurrentVersion(VERSION_STRING);
    LOG("OTA manager initialized successfully");
    LOGF("OTA Version: %s", VERSION_STRING);
#endif
    
    // Synchronize time with NTP server
    LOG("Synchronizing time with NTP server...");
    ScheduleManager* sm = uploader->getScheduleManager();
    if (sm && sm->isTimeSynced()) {
        LOG("Time synchronized successfully");
        LOGF("System time: %s", sm->getCurrentLocalTime().c_str());
    } else {
        LOG_DEBUG("Time sync not yet available, will retry every 5 minutes");
        lastNtpSyncAttempt = millis();
    }

#ifdef ENABLE_WEBSERVER
    // Initialize CPAP monitor
#ifdef ENABLE_CPAP_MONITOR
    LOG("Initializing CPAP SD card usage monitor...");
    cpapMonitor = new CPAPMonitor();
    cpapMonitor->begin();
    LOG("CPAP monitor started - tracking SD card usage every 10 minutes");
#else
    LOG("CPAP monitor disabled (CS_SENSE hardware issue)");
    cpapMonitor = new CPAPMonitor();  // Use stub implementation
#endif
    
    // Initialize web server
    LOG("Initializing web server...");
    
    // Create web server with references to uploader's internal components
    webServer = new CpapWebServer(&config, 
                                      uploader->getStateManager(),
                                      uploader->getScheduleManager(),
                                      &wifiManager,
                                      cpapMonitor);
    
    if (webServer->begin()) {
        LOG("Web server started successfully");
        LOGF("Access web interface at: http://%s", wifiManager.getIPAddress().c_str());
        
#ifdef ENABLE_OTA_UPDATES
        // Set OTA manager reference in web server
        webServer->setOTAManager(&otaManager);
        LOG_DEBUG("OTA manager linked to web server");
#endif
        
        // Set TrafficMonitor reference in web server for SD Activity Monitor
        webServer->setTrafficMonitor(&trafficMonitor);
        LOG_DEBUG("TrafficMonitor linked to web server");

        webServer->setSdManager(&sdManager);
        LOG_DEBUG("SDCardManager linked to web server for config editor");

        // Give web server access to the SMB state manager so updateStatusSnapshot()
        // can show folder counts from the active backend (SMB pass vs cloud pass).
        webServer->setSmbStateManager(uploader->getSmbStateManager());
        
        // Set web server reference in uploader for responsive handling during uploads
        if (uploader) {
            uploader->setWebServer(webServer);
            LOG_DEBUG("Web server linked to uploader for responsive handling");
        }

        // Build static config snapshot once — served from g_webConfigBuf with zero heap alloc.
        webServer->initConfigSnapshot();
        LOG_DEBUG("[WebStatus] Config snapshot built");
    } else {
        LOG_ERROR("Failed to start web server");
    }
#endif

    // Set initial FSM state based on upload mode
    if (uploader && uploader->getScheduleManager() && uploader->getScheduleManager()->isSmartMode()) {
        LOG("[FSM] Smart mode — starting in LISTENING (continuous loop)");
        transitionTo(UploadState::LISTENING);
    } else {
        LOG("[FSM] Scheduled mode — starting in IDLE");
        // IDLE is the correct initial state for scheduled mode
    }

    LOG("Setup complete!");
}

// ============================================================================
// FSM State Handlers
// ============================================================================

void handleIdle() {
    // IDLE is only used in scheduled mode.
    // Smart mode never enters IDLE — it uses the continuous loop:
    // LISTENING → ACQUIRING → UPLOADING → RELEASING → COOLDOWN → LISTENING
    
    unsigned long now = millis();
    if (now - lastIdleCheck < IDLE_CHECK_INTERVAL_MS) return;
    lastIdleCheck = now;
    
    if (!uploader || !uploader->getScheduleManager()) return;
    
    ScheduleManager* sm = uploader->getScheduleManager();
    
    // In scheduled mode: transition to LISTENING when the upload window opens,
    // even if all known files are marked complete. This ensures new DATALOG
    // folders written by the CPAP since the last upload are discovered during
    // the scan phase of the upload cycle.
    if (sm->isInUploadWindow() && !sm->isDayCompleted()) {
        LOG("[FSM] Upload window open — transitioning to LISTENING");
        transitionTo(UploadState::LISTENING);
    }
}

void handleListening() {
    // TrafficMonitor.update() is called in main loop before FSM dispatch
    uint32_t inactivityMs = (uint32_t)config.getInactivitySeconds() * 1000UL;

    // Config edit lock — pause upload until user saves or cancels
    if (g_configEditLock) {
        if (millis() - g_configEditLockAt > CONFIG_EDIT_LOCK_TIMEOUT_MS) {
            LOG_WARN("[FSM] Config edit lock expired (30 min) — auto-releasing");
            g_configEditLock = false;
        } else {
            return;  // Hold in LISTENING — don't start upload
        }
    }

    if (trafficMonitor.isIdleFor(inactivityMs)) {
        LOGF("[FSM] %ds of bus silence confirmed", config.getInactivitySeconds());
        transitionTo(UploadState::ACQUIRING);
        return;
    }
    
    // In scheduled mode, check if the upload window has closed while we were listening
    // Smart mode never exits LISTENING to IDLE — it stays in the continuous loop
    ScheduleManager* sm = uploader->getScheduleManager();
    if (!sm->isSmartMode()) {
        if (!sm->isInUploadWindow() || sm->isDayCompleted()) {
            LOG("[FSM] Scheduled mode — window closed or day completed while listening");
            transitionTo(UploadState::IDLE);
        }
    }
}

void handleAcquiring() {
    if (sdManager.takeControl()) {
        LOG("[FSM] SD card control acquired");
        // Arm yield interrupt: CPAP CS assertion while we own bus = yield
        g_cpapYieldRequest = false;
        attachInterrupt(digitalPinToInterrupt(CS_SENSE), cpapYieldISR, FALLING);
        transitionTo(UploadState::UPLOADING);
    } else {
        LOG_WARN("[FSM] Failed to acquire SD card, releasing to cooldown");
        transitionTo(UploadState::RELEASING);
    }
}

// FreeRTOS task function — runs on Core 0 so main loop (Core 1) stays responsive
void uploadTaskFunction(void* pvParameters) {
    UploadTaskParams* params = (UploadTaskParams*)pvParameters;
    
    g_uploadHeartbeat = millis();
    
    UploadResult result = params->uploader->uploadWithExclusiveAccess(
        params->sdManager, params->maxMinutes, params->filter);
    
    uploadTaskResult = result;
    uploadTaskComplete = true;
    
    delete params;
    vTaskDelete(NULL);  // Self-delete
}

static void runUploadBlocking(DataFilter filter) {
    int maxMinutes = config.getExclusiveAccessMinutes();
    UploadResult result = uploader->uploadWithExclusiveAccess(&sdManager, maxMinutes, filter);
    switch (result) {
        case UploadResult::COMPLETE:
            transitionTo(UploadState::COMPLETE);
            break;
        case UploadResult::TIMEOUT:
            uploadCycleHadTimeout = true;
            transitionTo(UploadState::RELEASING);
            break;
        case UploadResult::ERROR:
            LOG_ERROR("[FSM] Upload error");
            transitionTo(UploadState::RELEASING);
            break;
        case UploadResult::NOTHING_TO_DO:
            LOG("[FSM] Nothing to upload — entering cooldown (no reboot)");
            g_nothingToUpload = true;
            transitionTo(UploadState::RELEASING);
            break;
        case UploadResult::YIELD_NEEDED:
            LOG("[FSM] CPAP requested bus — yielding, will retry");
            transitionTo(UploadState::RELEASING);
            break;
    }
}

void handleUploading() {
    if (!uploader) {
        transitionTo(UploadState::RELEASING);
        return;
    }
    
    if (!uploadTaskRunning) {
        // ── First call: determine filter and spawn upload task ──
        ScheduleManager* sm = uploader->getScheduleManager();
        DataFilter filter;
        bool canFresh = sm->canUploadFreshData();
        bool canOld = sm->canUploadOldData();
        
        if (canFresh && canOld) {
            filter = DataFilter::ALL_DATA;
        } else if (canFresh) {
            filter = DataFilter::FRESH_ONLY;
        } else if (canOld) {
            filter = DataFilter::OLD_ONLY;
        } else {
            LOG_WARN("[FSM] No data category eligible, releasing");
            transitionTo(UploadState::RELEASING);
            return;
        }

        const uint32_t freeHeapBeforeTask = ESP.getFreeHeap();
        const uint32_t maxAllocBeforeTask = ESP.getMaxAllocHeap();
        if (maxAllocBeforeTask < UPLOAD_ASYNC_MIN_MAX_ALLOC_BYTES) {
            LOG_WARNF("[FSM] Low contiguous heap (%u bytes, free=%u) - using blocking upload to preserve memory",
                      maxAllocBeforeTask,
                      freeHeapBeforeTask);
            runUploadBlocking(filter);
            return;
        }
        
        // Disable web server handling inside upload task — main loop handles it
        // This prevents concurrent handleClient() calls from two cores
#ifdef ENABLE_WEBSERVER
        uploader->setWebServer(nullptr);
#endif
        
        UploadTaskParams* params = new UploadTaskParams{
            uploader, &sdManager, config.getExclusiveAccessMinutes(), filter
        };
        
        uploadTaskComplete = false;
        uploadTaskRunning = true;
        
        // Unsubscribe IDLE0 from task watchdog — upload task will monopolize Core 0
        // during TLS handshake (5-15s of CPU-intensive crypto), starving IDLE0.
        // Without this, IDLE0 can't feed the WDT and the system reboots.
        esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
        
        // Pin to Core 0 (protocol core) — keeps Core 1 free for main loop + web server
        // Stack: 16KB — TLS buffers are on heap, task only needs stack for locals
        BaseType_t rc = xTaskCreatePinnedToCore(
            uploadTaskFunction,  // Task function
            "upload",            // Name
            16384,               // Stack size (16KB for task locals + SD I/O)
            params,              // Parameters
            1,                   // Priority (same as loop task)
            &uploadTaskHandle,   // Handle
            0                    // Pin to Core 0
        );
        
        if (rc != pdPASS) {
            LOG_ERRORF("[FSM] Failed to create upload task (rc=%ld, free=%u, max_alloc=%u) — falling back to blocking upload",
                       (long)rc,
                       ESP.getFreeHeap(),
                       ESP.getMaxAllocHeap());
            uploadTaskRunning = false;
            delete params;
            // Re-subscribe IDLE0 since task didn't start
            esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
#ifdef ENABLE_WEBSERVER
            uploader->setWebServer(webServer);
#endif
            // Fallback: run synchronously (old behavior)
            runUploadBlocking(filter);
        } else {
            LOG("[FSM] Upload task started on Core 0 (non-blocking)");
        }
    } else if (uploadTaskComplete) {
        // ── Task finished: read result and transition ──
        uploadTaskRunning = false;
        uploadTaskHandle = nullptr;
        
        // Re-subscribe IDLE0 to task watchdog now that Core 0 is free
        esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
        
        // Restore web server handling in uploader
#ifdef ENABLE_WEBSERVER
        uploader->setWebServer(webServer);
#endif
        
        UploadResult result = (UploadResult)uploadTaskResult;
        
        switch (result) {
            case UploadResult::COMPLETE:
                transitionTo(UploadState::COMPLETE);
                break;
            case UploadResult::TIMEOUT:
                uploadCycleHadTimeout = true;
                transitionTo(UploadState::RELEASING);
                break;
            case UploadResult::ERROR:
                LOG_ERROR("[FSM] Upload error occurred");
                transitionTo(UploadState::RELEASING);
                break;
            case UploadResult::NOTHING_TO_DO:
                LOG("[FSM] Nothing to upload — releasing SD and entering cooldown (no reboot)");
                g_nothingToUpload = true;
                transitionTo(UploadState::RELEASING);
                break;
            case UploadResult::YIELD_NEEDED:
                LOG("[FSM] CPAP requested bus — yielding, will retry after cooldown");
                transitionTo(UploadState::RELEASING);
                break;
        }
    }
    // else: task still running — return immediately (non-blocking)
}

void handleReleasing() {
    // Disarm yield interrupt before releasing bus
    detachInterrupt(digitalPinToInterrupt(CS_SENSE));
    g_cpapYieldRequest = false;

    if (sdManager.hasControl()) {
        sdManager.releaseControl();
    }
    
    // If monitoring was requested during upload, go to MONITORING instead of COOLDOWN
    if (monitoringRequested) {
        monitoringRequested = false;
        trafficMonitor.resetStatistics();
        LOG("[FSM] Monitoring requested during upload — entering MONITORING after release");
        transitionTo(UploadState::MONITORING);
        return;
    }

    // If nothing was uploaded, skip the reboot and go straight to cooldown.
    // This prevents an endless reboot cycle when all backends are already synced.
    if (g_nothingToUpload) {
        g_nothingToUpload = false;
        LOGF("[FSM] Nothing to upload — entering cooldown without reboot (fh=%u ma=%u)",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        cooldownStartedAt = millis();
        transitionTo(UploadState::COOLDOWN);
        return;
    }

    // Otherwise always soft-reboot after a real upload session.
    // A clean reboot restores the full contiguous heap and keeps the FSM simple.
    // The fast-boot path (ESP_RST_SW) skips cold-boot delays.
    LOGF("[FSM] Upload session complete — soft-reboot to restore heap (fh=%u ma=%u)",
         (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    delay(200);
    esp_restart();
}

void handleCooldown() {
    unsigned long cooldownMs = (unsigned long)config.getCooldownMinutes() * 60UL * 1000UL;
    
    if (millis() - cooldownStartedAt < cooldownMs) {
        return;  // Non-blocking wait
    }
    
    LOGF("[FSM] Cooldown complete (%d minutes)", config.getCooldownMinutes());
    uploadCycleHadTimeout = false;
    
    ScheduleManager* sm = uploader->getScheduleManager();
    
    if (sm->isSmartMode()) {
        // Smart mode: ALWAYS return to LISTENING (continuous loop)
        trafficMonitor.resetIdleTracking();
        LOG("[FSM] Smart mode — returning to LISTENING (continuous loop)");
        transitionTo(UploadState::LISTENING);
    } else {
        // Scheduled mode: return to LISTENING if still in window and day not done
        if (sm->isInUploadWindow() && !sm->isDayCompleted()) {
            trafficMonitor.resetIdleTracking();
            transitionTo(UploadState::LISTENING);
        } else {
            LOG("[FSM] Scheduled mode — window closed or day completed");
            transitionTo(UploadState::IDLE);
        }
    }
}

void handleComplete() {
    ScheduleManager* sm = uploader->getScheduleManager();
    
    if (sm->isSmartMode()) {
        // Smart mode: release → cooldown → listening (continuous loop)
        // Next cycle will scan SD card and discover any new data naturally
        LOG("[FSM] Smart mode complete — continuing loop via RELEASING → COOLDOWN → LISTENING");
        transitionTo(UploadState::RELEASING);
    } else {
        // Scheduled mode: done for today
        sm->markDayCompleted();
        LOG("[FSM] Scheduled mode — day marked as completed");
        transitionTo(UploadState::IDLE);
    }
}

void handleMonitoring() {
    // TrafficMonitor.update() runs as normal (called in main loop)
    // No upload activity, no SD card access
    // Web endpoint /api/sd-activity serves live PCNT sample data
    
    if (stopMonitoringRequested) {
        stopMonitoringRequested = false;
        LOG("[FSM] Monitoring stopped by user");
        ScheduleManager* sm = uploader ? uploader->getScheduleManager() : nullptr;
        if (sm && sm->isSmartMode()) {
            trafficMonitor.resetIdleTracking();
            transitionTo(UploadState::LISTENING);
        } else {
            transitionTo(UploadState::IDLE);
        }
    }
}

// ============================================================================
// Loop Function
// ============================================================================
void loop() {
    // ── Always-on tasks ──
    
    // Periodic SD card log dump (every 10 seconds when enabled)
    // Skip when upload task is running — SD card is in use on another core
    if (config.getLogToSdCard() && !uploadTaskRunning) {
        unsigned long currentTime = millis();
        if (currentTime - lastLogDumpTime >= LOG_DUMP_INTERVAL_MS) {
            if (Logger::getInstance().dumpLogsToSDCardPeriodic(&sdManager)) {
                LOG_DEBUG("Periodic log dump to SD card completed");
            }
            lastLogDumpTime = currentTime;
        }
    }
    
    // Update traffic monitor only in states that need activity detection
    // LISTENING: needs idle detection to trigger uploads
    // MONITORING: needs live data for web UI
    if (currentState == UploadState::LISTENING || currentState == UploadState::MONITORING) {
        trafficMonitor.update();
    }

#ifdef ENABLE_WEBSERVER
    // Update CPAP monitor
#ifdef ENABLE_CPAP_MONITOR
    if (cpapMonitor) {
        cpapMonitor->update();
    }
#endif
    
    // Handle web server requests
    if (webServer) {
        webServer->handleClient();
        // Refresh status snapshot every 3 s — assembles JSON using snprintf into
        // g_webStatusBuf (stack only, zero heap allocation).
        static unsigned long lastStatusSnapMs = 0;
        if (millis() - lastStatusSnapMs >= 3000) {
            webServer->updateStatusSnapshot();
            lastStatusSnapMs = millis();
        }
    }
    
    // ── Software watchdog for upload task ──
    // If the upload task hasn't sent a heartbeat in UPLOAD_WATCHDOG_TIMEOUT_MS, it's hung.
    // Force-kill it and reboot — vTaskDelete mid-SD-I/O corrupts the SD bus,
    // making remount impossible. A clean reboot is the only reliable recovery.
    if (uploadTaskRunning && g_uploadHeartbeat > 0 &&
        (millis() - g_uploadHeartbeat > UPLOAD_WATCHDOG_TIMEOUT_MS)) {
        LOG_ERROR("[FSM] Upload task appears hung (no heartbeat for 2 minutes) — rebooting");
        
        // Set NVS flag so we can log the reason on next boot
        Preferences wdPrefs;
        wdPrefs.begin("cpap_flags", false);
        wdPrefs.putBool("watchdog_kill", true);
        wdPrefs.end();
        
        if (uploadTaskHandle) {
            vTaskDelete(uploadTaskHandle);
        }
        
        delay(300);
        esp_restart();
    }
    
    // ── Web trigger handlers (operate independently of FSM) ──
    
    // Check for state reset trigger — takes effect IMMEDIATELY, even during upload.
    // Strategy: set NVS flag → kill upload task → reboot.
    // State files are deleted on next boot with a clean SD card mount.
    // This avoids SD card access after killing a task mid-I/O (which can hang).
    if (g_resetStateFlag) {
        LOG("=== State Reset Triggered via Web Interface ===");
        g_resetStateFlag = false;
        
        // Set NVS flag so state files are deleted on next clean boot
        Preferences resetPrefs;
        resetPrefs.begin("cpap_flags", false);
        resetPrefs.putBool("reset_state", true);
        resetPrefs.end();
        LOG("Reset flag saved to NVS");
        
        // Kill upload task if running (don't touch SD card after this!)
        if (uploadTaskRunning && uploadTaskHandle) {
            LOG_WARN("[FSM] Killing active upload task for state reset");
            vTaskDelete(uploadTaskHandle);
            uploadTaskRunning = false;
            uploadTaskHandle = nullptr;
        }
        
        // Immediate reboot — state files deleted on next clean boot
        LOG("Rebooting for clean state reset...");
        delay(300);  // Brief pause for web response to send
        esp_restart();
    }
    
    // Soft reboot — next boot detects ESP_RST_SW and skips all delays automatically
    if (g_softRebootFlag) {
        LOG("=== Soft Reboot Triggered via Web Interface ===");
        g_softRebootFlag = false;
        delay(300);
        esp_restart();
    }

    // Check for upload trigger (force immediate upload — skip inactivity check)
    // Blocked while upload task is running — already uploading
    if (g_triggerUploadFlag && !uploadTaskRunning) {
        LOG("=== Upload Triggered via Web Interface ===");
        g_triggerUploadFlag = false;
        uploadCycleHadTimeout = false;
        transitionTo(UploadState::ACQUIRING);
    }
    
    // Check for monitoring triggers
    if (g_monitorActivityFlag) {
        g_monitorActivityFlag = false;
        monitoringRequested = true;
    }
    if (g_stopMonitorFlag) {
        g_stopMonitorFlag = false;
        stopMonitoringRequested = true;
    }
#endif
    
    // ── WiFi reconnection (non-blocking with 30 second retry interval) ──
    if (!wifiManager.isConnected()) {
        unsigned long currentTime = millis();
        if (currentTime - lastWifiReconnectAttempt >= 30000) {
            LOG_WARN("WiFi disconnected, attempting to reconnect...");
            
            if (!config.valid() || config.getWifiSSID().isEmpty()) {
                LOG_ERROR("Cannot reconnect to WiFi: Invalid configuration");
                lastWifiReconnectAttempt = currentTime;
                return;
            }
            
            if (!wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword())) {
                LOG_ERROR("Failed to reconnect to WiFi");
                lastWifiReconnectAttempt = currentTime;
                return;
            }
            LOG_DEBUG("WiFi reconnected successfully");
            
            // Restart mDNS responder after reconnection
            wifiManager.startMDNS(config.getHostname());
            
            lastNtpSyncAttempt = 0;
            lastWifiReconnectAttempt = 0;
        }
        return;  // Skip FSM while WiFi is down
    }

    // ── NTP sync retry ──
    if (uploader) {
        unsigned long currentTime = millis();
        if (currentTime - lastNtpSyncAttempt >= NTP_RETRY_INTERVAL_MS) {
            LOG_DEBUG("Periodic NTP synchronization check...");
            lastNtpSyncAttempt = currentTime;
        }
    }
    
    // ── Monitoring request handling (can interrupt most states) ──
    if (monitoringRequested) {
        if (currentState != UploadState::UPLOADING) {
            monitoringRequested = false;
            if (currentState == UploadState::ACQUIRING && sdManager.hasControl()) {
                sdManager.releaseControl();
            }
            trafficMonitor.resetStatistics();
            transitionTo(UploadState::MONITORING);
        }
        // If UPLOADING, leave flag set — handleReleasing() will redirect to MONITORING
        // after upload finishes current cycle + mandatory root/SETTINGS files
    }

    // ── FSM dispatch ──
    switch (currentState) {
        case UploadState::IDLE:       handleIdle();       break;
        case UploadState::LISTENING:  handleListening();  break;
        case UploadState::ACQUIRING:  handleAcquiring();  break;
        case UploadState::UPLOADING:  handleUploading();  break;
        case UploadState::RELEASING:  handleReleasing();  break;
        case UploadState::COOLDOWN:   handleCooldown();   break;
        case UploadState::COMPLETE:   handleComplete();   break;
        case UploadState::MONITORING: handleMonitoring(); break;
    }
}
