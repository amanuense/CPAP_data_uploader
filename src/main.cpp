#include <Arduino.h>
#include "Config.h"
#include "SDCardManager.h"
#include "WiFiManager.h"
#include "FileUploader.h"
#include "Logger.h"

#ifdef ENABLE_TEST_WEBSERVER
#include "TestWebServer.h"
#endif

// ============================================================================
// Global Objects
// ============================================================================
Config config;
SDCardManager sdManager;
WiFiManager wifiManager;
FileUploader* uploader = nullptr;

#ifdef ENABLE_TEST_WEBSERVER
TestWebServer* testWebServer = nullptr;
#endif

// ============================================================================
// Global State
// ============================================================================
unsigned long lastNtpSyncAttempt = 0;
const unsigned long NTP_RETRY_INTERVAL_MS = 5 * 60 * 1000;  // 5 minutes
unsigned long nextUploadRetryTime = 0;
bool budgetExhaustedRetry = false;  // True if waiting due to budget exhaustion
unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastUploadCheck = 0;
unsigned long lastSdCardRetry = 0;

#ifdef ENABLE_TEST_WEBSERVER
// External trigger flags (defined in TestWebServer.cpp)
extern volatile bool g_triggerUploadFlag;
extern volatile bool g_resetStateFlag;
#endif

// ============================================================================
// Setup Function
// ============================================================================
void setup() {
    // Initialize serial port
    Serial.begin(115200);
    delay(1000);
    LOG("\n\n=== CPAP Data Auto-Uploader ===");

    // Initialize SD card control
    if (!sdManager.begin()) {
        LOG("Failed to initialize SD card manager");
        return;
    }

    // Take control of SD card
    LOG("Waiting to access SD card...");
    while (!sdManager.takeControl()) {
        delay(1000);
    }

    // Read config file from SD card
    LOG("Loading configuration...");
    if (!config.loadFromSD(sdManager.getFS())) {
        LOG("Failed to load configuration");
        sdManager.releaseControl();
        return;
    }

    LOG("Configuration loaded successfully");
    LOG_DEBUGF("WiFi SSID: %s", config.getWifiSSID().c_str());
    LOG_DEBUGF("Endpoint: %s", config.getEndpoint().c_str());

    // Release SD card back to CPAP machine
    sdManager.releaseControl();

    // Initialize WiFi in station mode
    if (!wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword())) {
        LOG("Failed to connect to WiFi");
        return;
    }

    // Initialize uploader
    uploader = new FileUploader(&config, &wifiManager);
    
    // Take control of SD card to initialize uploader components
    LOG("Initializing uploader...");
    if (sdManager.takeControl()) {
        if (!uploader->begin(sdManager.getFS())) {
            LOG("ERROR: Failed to initialize uploader");
            sdManager.releaseControl();
            return;
        }
        sdManager.releaseControl();
        LOG("Uploader initialized successfully");
    } else {
        LOG("ERROR: Failed to take SD card control for uploader initialization");
        return;
    }
    
    // Synchronize time with NTP server
    LOG("Synchronizing time with NTP server...");
    // The ScheduleManager handles NTP sync internally during begin()
    // We can check if time is synced by calling shouldUpload()
    // (it will return false if time is not synced)
    
    // Trigger initial time sync check
    if (uploader->shouldUpload()) {
        LOG("Time synchronized successfully");
        LOG_DEBUG("Currently in upload window - will begin upload shortly");
    } else {
        LOG_DEBUG("Time sync status unknown or not in upload window");
        LOG_DEBUG("Will retry NTP sync every 5 minutes if needed");
        lastNtpSyncAttempt = millis();
    }

#ifdef ENABLE_TEST_WEBSERVER
    // Initialize test web server for on-demand testing
    LOG("Initializing test web server...");
    
    // Create test web server with references to uploader's internal components
    testWebServer = new TestWebServer(&config, 
                                      uploader->getStateManager(),
                                      uploader->getBudgetManager(),
                                      uploader->getScheduleManager());
    
    if (testWebServer->begin()) {
        LOG("Test web server started successfully");
        LOGF("Access web interface at: http://%s", wifiManager.getIPAddress().c_str());
    } else {
        LOG("ERROR: Failed to start test web server");
    }
#endif

    LOG("Setup complete!");
}

// ============================================================================
// Loop Function
// ============================================================================
void loop() {
#ifdef ENABLE_TEST_WEBSERVER
    // Handle web server requests
    if (testWebServer) {
        testWebServer->handleClient();
    }
    
    // Check for state reset trigger
    if (g_resetStateFlag) {
        LOG("=== State Reset Triggered via Web Interface ===");
        g_resetStateFlag = false;
        
        // Take SD card control to reset state
        if (sdManager.takeControl()) {
            LOG("Resetting upload state...");
            
            // Delete the state file
            if (sdManager.getFS().remove("/.upload_state.json")) {
                LOG("Upload state file deleted successfully");
            } else {
                LOG("WARNING: Failed to delete state file (may not exist)");
            }
            
            // Reinitialize uploader to load fresh state
            if (uploader) {
                delete uploader;
                uploader = new FileUploader(&config, &wifiManager);
                if (uploader->begin(sdManager.getFS())) {
                    LOG("Uploader reinitialized with fresh state");
                    
                    // Update TestWebServer with new manager references
                    if (testWebServer) {
                        testWebServer->updateManagers(uploader->getStateManager(),
                                                     uploader->getBudgetManager(),
                                                     uploader->getScheduleManager());
                        LOG_DEBUG("TestWebServer manager references updated");
                    }
                } else {
                    LOG("ERROR: Failed to reinitialize uploader");
                }
            }
            
            sdManager.releaseControl();
            LOG("State reset complete");
        } else {
            LOG("ERROR: Cannot reset state - SD card in use");
            LOG("Will retry on next loop iteration");
        }
    }
    
    // Check for upload trigger
    if (g_triggerUploadFlag) {
        LOG("=== Upload Triggered via Web Interface ===");
        g_triggerUploadFlag = false;
        
        // Force upload regardless of schedule
        // We'll bypass the shouldUpload() check and go straight to upload
        LOG("Forcing immediate upload session...");
        
        // Try to take control of SD card
        if (sdManager.takeControl()) {
            LOG("SD card control acquired, starting forced upload...");
            
            // Perform upload with force flag to bypass schedule check
            bool uploadSuccess = uploader->uploadNewFiles(sdManager.getFS(), true);
            
            // Release SD card
            sdManager.releaseControl();
            LOG("SD card control released");
            
            if (uploadSuccess) {
                LOG("Forced upload completed successfully");
            } else {
                LOG("Forced upload incomplete (budget exhausted or errors)");
            }
        } else {
            LOG("ERROR: Cannot start upload - SD card in use by CPAP");
            LOG("Will retry on next loop iteration");
        }
    }
#endif
    
    // Check WiFi connection (non-blocking with 30 second retry interval)
    if (!wifiManager.isConnected()) {
        unsigned long currentTime = millis();
        if (currentTime - lastWifiReconnectAttempt >= 30000) {
            LOG("WARNING: WiFi disconnected, attempting to reconnect...");
            if (!wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword())) {
                LOG("ERROR: Failed to reconnect to WiFi");
                LOG("Will retry in 30 seconds...");
                lastWifiReconnectAttempt = currentTime;
                return;
            }
            LOG_DEBUG("WiFi reconnected successfully");
            
            // Reset NTP sync attempt timer to trigger immediate sync after reconnection
            lastNtpSyncAttempt = 0;
            lastWifiReconnectAttempt = 0;
        }
        return;  // Skip rest of loop while WiFi is down
    }

    // Handle NTP sync retry if time is not synchronized
    // This runs periodically to ensure time stays synchronized
    if (uploader) {
        unsigned long currentTime = millis();
        if (currentTime - lastNtpSyncAttempt >= NTP_RETRY_INTERVAL_MS) {
            LOG_DEBUG("Periodic NTP synchronization check...");
            // Note: shouldUpload() checks time sync internally
            // We just want to trigger the check periodically
            lastNtpSyncAttempt = currentTime;
        }
    }

    // Check if we're waiting due to budget exhaustion (non-blocking)
    if (budgetExhaustedRetry) {
        // Wait for the appropriate time before retrying
        if (millis() < nextUploadRetryTime) {
            return;  // Non-blocking wait
        }
        // Wait period over, clear the flag and continue
        budgetExhaustedRetry = false;
        LOG_DEBUG("Budget exhaustion wait period complete, resuming upload...");
    }

    // Check if it's time to upload (scheduled window - once per day)
    // Use non-blocking check every 60 seconds
    unsigned long currentTime = millis();
    if (currentTime - lastUploadCheck < 60000) {
        return;  // Don't check too frequently
    }
    lastUploadCheck = currentTime;
    
    if (!uploader || !uploader->shouldUpload()) {
        // Not upload time yet
        return;
    }

    LOG("=== Upload Window Active ===");
    LOG("Attempting to start upload session...");

    // Try to take control of SD card for upload session (non-blocking retry)
    if (!sdManager.takeControl()) {
        unsigned long now = millis();
        if (now - lastSdCardRetry >= 5000) {
            LOG("CPAP machine is using SD card, will retry in 5 seconds...");
            lastSdCardRetry = now;
        }
        return;
    }
    lastSdCardRetry = 0;  // Reset retry timer on success

    LOG("SD card control acquired, starting upload session...");

    // Perform upload session
    // Note: uploadNewFiles() handles:
    // - Time budget enforcement
    // - File prioritization (DATALOG newest first, then root/SETTINGS)
    // - State persistence
    // - Retry count management
    bool uploadSuccess = uploader->uploadNewFiles(sdManager.getFS());

    // Release SD card back to CPAP machine
    sdManager.releaseControl();
    LOG("SD card control released");

    // Determine if we need to wait before retrying
    // The upload can fail for two reasons:
    // 1. Budget exhausted (partial upload) - need to wait 2x session duration
    // 2. All files uploaded or scheduled upload complete - wait until next day
    
    if (uploadSuccess) {
        LOG("=== Upload Session Completed Successfully ===");
        LOG_DEBUG("All pending files have been uploaded");
        LOG_DEBUG("Next upload will occur at scheduled time tomorrow");
        // The ScheduleManager has already marked upload as completed
        // No need to set retry timer - will wait for next scheduled time
    } else {
        LOG("=== Upload Session Incomplete ===");
        LOG("Session ended due to time budget exhaustion or errors");
        
        // Calculate wait time (2x session duration) before retry
        unsigned long sessionDuration = config.getSessionDurationSeconds() * 1000;
        unsigned long waitTime = sessionDuration * 2;
        
        LOGF("Waiting %lu seconds before retry...", waitTime / 1000);
        LOG_DEBUG("This allows CPAP machine priority access to SD card");
        
        nextUploadRetryTime = millis() + waitTime;
        budgetExhaustedRetry = true;
        
        // Note: We stay in the same upload window (same day)
        // The ScheduleManager will NOT mark upload as completed
        // So shouldUpload() will continue to return true after wait period
    }
}
