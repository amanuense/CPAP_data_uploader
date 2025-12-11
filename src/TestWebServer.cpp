#include "TestWebServer.h"
#include "Logger.h"
#include <time.h>

// Global trigger flags
volatile bool g_triggerUploadFlag = false;
volatile bool g_resetStateFlag = false;
volatile bool g_scanNowFlag = false;

// External retry timing variables (defined in main.cpp)
extern unsigned long nextUploadRetryTime;
extern bool budgetExhaustedRetry;

// Constructor
TestWebServer::TestWebServer(Config* cfg, UploadStateManager* state,
                             TimeBudgetManager* budget, ScheduleManager* schedule, 
                             WiFiManager* wifi, CPAPMonitor* monitor)
    : server(nullptr),
      config(cfg),
      stateManager(state),
      budgetManager(budget),
      scheduleManager(schedule),
      wifiManager(wifi),
      cpapMonitor(monitor) {
}

// Destructor
TestWebServer::~TestWebServer() {
    if (server) {
        server->stop();
        delete server;
    }
}

// Initialize and start the web server
bool TestWebServer::begin() {
    LOG("[TestWebServer] Initializing web server on port 80...");
    
    server = new WebServer(80);
    
    // Register request handlers
    server->on("/", [this]() { this->handleRoot(); });
    server->on("/trigger-upload", [this]() { this->handleTriggerUpload(); });
    server->on("/scan-now", [this]() { this->handleScanNow(); });
    server->on("/status", [this]() { this->handleStatus(); });
    server->on("/reset-state", [this]() { this->handleResetState(); });
    server->on("/config", [this]() { this->handleConfig(); });
    server->on("/logs", [this]() { this->handleLogs(); });
    
    // Handle common browser requests silently
    server->on("/favicon.ico", [this]() { 
        // Return empty 204 No Content to avoid error logs
        server->send(204); 
    });
    
    server->onNotFound([this]() { this->handleNotFound(); });
    
    // Start the server
    server->begin();
    
    LOG("[TestWebServer] Web server started successfully");
    LOG("[TestWebServer] Available endpoints:");
    LOG("[TestWebServer]   GET /              - Status page (HTML)");
    LOG("[TestWebServer]   GET /trigger-upload - Force immediate upload");
    LOG("[TestWebServer]   GET /scan-now      - Scan SD card for pending folders");
    LOG("[TestWebServer]   GET /status        - Status information (JSON)");
    LOG("[TestWebServer]   GET /reset-state   - Clear upload state");
    LOG("[TestWebServer]   GET /config        - Display configuration");
    LOG("[TestWebServer]   GET /logs          - Retrieve system logs (JSON)");
    
    return true;
}

// Process incoming HTTP requests
void TestWebServer::handleClient() {
    if (server) {
        server->handleClient();
    }
}

// GET / - HTML status page
void TestWebServer::handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>CPAP Auto-Uploader Status</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
    html += "h1 { color: #333; }";
    html += "h2 { color: #666; margin-top: 30px; }";
    html += ".container { background: white; padding: 20px; border-radius: 8px; max-width: 800px; }";
    html += ".info { margin: 10px 0; }";
    html += ".label { font-weight: bold; display: inline-block; width: 200px; }";
    html += ".value { color: #0066cc; }";
    html += ".button { display: inline-block; padding: 10px 20px; margin: 10px 5px; ";
    html += "background: #0066cc; color: white; text-decoration: none; border-radius: 4px; }";
    html += ".button:hover { background: #0052a3; }";
    html += ".button.danger { background: #cc0000; }";
    html += ".button.danger:hover { background: #a30000; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>CPAP Auto-Uploader Status</h1>";
    
    // System information
    html += "<h2>System Information</h2>";
    html += "<div class='info'><span class='label'>Uptime:</span><span class='value'>" + getUptimeString() + "</span></div>";
    html += "<div class='info'><span class='label'>Current Time:</span><span class='value'>" + getCurrentTimeString() + "</span></div>";
    html += "<div class='info'><span class='label'>Free Heap:</span><span class='value'>" + String(ESP.getFreeHeap()) + " bytes</span></div>";
    
    // WiFi information
    if (wifiManager && wifiManager->isConnected()) {
        int rssi = wifiManager->getSignalStrength();
        String quality = wifiManager->getSignalQuality();
        String qualityColor = "#0066cc";
        
        // Color code based on signal quality
        if (quality == "Excellent") {
            qualityColor = "#00cc00";
        } else if (quality == "Good") {
            qualityColor = "#66cc00";
        } else if (quality == "Fair") {
            qualityColor = "#cc9900";
        } else if (quality == "Weak") {
            qualityColor = "#cc6600";
        } else if (quality == "Very Weak") {
            qualityColor = "#cc0000";
        }
        
        html += "<div class='info'><span class='label'>WiFi Signal:</span><span class='value' style='color:" + qualityColor + ";'>";
        html += quality + " (" + String(rssi) + " dBm)</span></div>";
    }
    
    // Upload status
    html += "<h2>Upload Status</h2>";
    if (scheduleManager) {
        unsigned long secondsUntilNext = scheduleManager->getSecondsUntilNextUpload();
        html += "<div class='info'><span class='label'>Next Upload In:</span><span class='value'>";
        if (secondsUntilNext == 0) {
            html += "Upload window active";
        } else {
            html += String(secondsUntilNext / 3600) + " hours, ";
            html += String((secondsUntilNext % 3600) / 60) + " minutes";
        }
        html += "</span></div>";
        html += "<div class='info'><span class='label'>Upload Time Synced:</span><span class='value'>";
        html += scheduleManager->isTimeSynced() ? "Yes" : "No";
        html += "</span></div>";
    } else {
        html += "<div class='info'><span class='label'>Status:</span><span class='value'>Initializing...</span></div>";
    }
    
    if (budgetManager) {
        unsigned long remainingBudget = budgetManager->getRemainingBudgetMs();
        html += "<div class='info'><span class='label'>Time Budget Remaining:</span><span class='value'>";
        if (remainingBudget == 0) {
            html += "No active session";
        } else {
            html += String(remainingBudget) + " ms";
        }
        html += "</span></div>";
        
        unsigned long rate = budgetManager->getTransmissionRate();
        float rateKB = rate / 1024.0;
        html += "<div class='info'><span class='label'>Transfer Rate:</span><span class='value'>";
        html += String(rateKB, 1) + " KB/s (" + String(rate) + " B/s)</span></div>";
    } else {
        html += "<div class='info'><span class='label'>Budget:</span><span class='value'>Not initialized</span></div>";
    }
    
    // Upload progress
    if (stateManager) {
        int completedFolders = stateManager->getCompletedFoldersCount();
        int incompleteFolders = stateManager->getIncompleteFoldersCount();
        int pendingFolders = stateManager->getPendingFoldersCount();
        int totalFolders = completedFolders + incompleteFolders + pendingFolders;
        
        if (totalFolders == 0) {
            // State not yet initialized (no upload session has run)
            html += "<div class='info'><span class='label'>Upload Status:</span><span class='value'>";
            html += "Not yet scanned (waiting for first upload window)</span></div>";
        } else {
            html += "<div class='info'><span class='label'>Upload Progress:</span><span class='value'>";
            if (pendingFolders > 0) {
                html += String(completedFolders) + " / " + String(totalFolders) + " folders completed, " + String(pendingFolders) + " empty</span></div>";
            } else {
                html += String(completedFolders) + " / " + String(totalFolders) + " folders completed</span></div>";
            }
            
            if (incompleteFolders > 0) {
                html += "<div class='info'><span class='label'>Incomplete Folders:</span><span class='value'>";
                html += String(incompleteFolders) + "</span></div>";
            }
        }
        
        // Show retry information if applicable
        String retryFolder = stateManager->getCurrentRetryFolder();
        if (!retryFolder.isEmpty()) {
            int retryCount = stateManager->getCurrentRetryCount();
            html += "<div class='info'><span class='label'>Current Retry:</span><span class='value' style='color: #cc6600;'>";
            html += "Folder " + retryFolder + " (attempt " + String(retryCount + 1) + ")</span></div>";
        }
    } else {
        html += "<div class='info'><span class='label'>Pending Folders:</span><span class='value'>Unknown</span></div>";
    }
    
    // Retry warning
    if (stateManager) {
        String retryFolder = stateManager->getCurrentRetryFolder();
        if (!retryFolder.isEmpty()) {
            int retryCount = stateManager->getCurrentRetryCount();
            int maxRetries = config ? config->getMaxRetryAttempts() : 3;
            
            html += "<h2 style='color: #cc6600;'>WARNING: Upload in Progress</h2>";
            html += "<div style='background: #fff3cd; border: 1px solid #ffc107; padding: 15px; border-radius: 4px; margin: 10px 0;'>";
            html += "<p><strong>Folder:</strong> " + retryFolder + "</p>";
            html += "<p><strong>Attempt:</strong> " + String(retryCount + 1) + " of " + String(maxRetries) + "</p>";
            html += "<p><strong>Reason:</strong> Upload session time budget exhausted before completing all files.</p>";
            
            // Show retry wait time if waiting
            if (budgetExhaustedRetry && nextUploadRetryTime > millis()) {
                unsigned long remainingMs = nextUploadRetryTime - millis();
                unsigned long remainingSeconds = remainingMs / 1000;
                unsigned long remainingMinutes = remainingSeconds / 60;
                
                html += "<p><strong>Next Retry In:</strong> ";
                if (remainingMinutes > 0) {
                    html += String(remainingMinutes) + " minutes " + String(remainingSeconds % 60) + " seconds";
                } else {
                    html += String(remainingSeconds) + " seconds";
                }
                html += "</p>";
            }
            
            if (retryCount >= 2) {
                html += "<p style='color: #cc0000;'><strong>WARNING: Multiple retries detected!</strong></p>";
                html += "<p>Consider increasing <code>SESSION_DURATION_SECONDS</code> in config.json if uploads consistently fail.</p>";
                html += "<p>Current session duration: " + String(config ? config->getSessionDurationSeconds() : 0) + " seconds (active time)</p>";
            }
            
            html += "</div>";
        }
    }

#ifdef ENABLE_CPAP_MONITOR
    // CPAP SD Card Usage Monitor
    if (cpapMonitor) {
        html += "<h2>CPAP SD Card Usage (24 Hours)</h2>";
        html += "<div class='info'><span class='label'>Monitoring Interval:</span><span class='value'>Every 10 minutes</span></div>";
        
        // Add the usage table
        html += cpapMonitor->getUsageTableHTML();
    }
#endif //ENABLE_CPAP_MONITOR

    
    // Configuration
    html += "<h2>Configuration</h2>";
    if (config) {
        html += "<div class='info'><span class='label'>Endpoint Type:</span><span class='value'>" + config->getEndpointType() + "</span></div>";
        html += "<div class='info'><span class='label'>Endpoint:</span><span class='value'>" + config->getEndpoint() + "</span></div>";
        html += "<div class='info'><span class='label'>Upload Hour:</span><span class='value'>" + String(config->getUploadHour()) + ":00</span></div>";
        html += "<div class='info'><span class='label'>Session Duration:</span><span class='value'>" + String(config->getSessionDurationSeconds()) + " seconds</span></div>";
    }
    
    // Action buttons
    html += "<h2>Actions</h2>";
    html += "<a href='/trigger-upload' class='button'>Trigger Upload Now</a>";
    html += "<a href='/scan-now' class='button'>Scan SD Card</a>";
    html += "<a href='/status' class='button'>View JSON Status</a>";
    html += "<a href='/config' class='button'>View Full Config</a>";
    html += "<a href='/logs' class='button'>View System Logs</a>";
    html += "<a href='/reset-state' class='button danger' onclick='return confirm(\"Are you sure you want to reset upload state?\")'>Reset Upload State</a>";
    
    html += "</div></body></html>";
    
    server->send(200, "text/html", html);
}

// GET /trigger-upload - Force immediate upload
void TestWebServer::handleTriggerUpload() {
    LOG("[TestWebServer] Upload trigger requested via web interface");
    
    // Set global trigger flag
    g_triggerUploadFlag = true;
    
    // Add CORS headers
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String response = "{\"status\":\"success\",\"message\":\"Upload triggered. Check serial output for progress.\"}";
    server->send(200, "application/json", response);
}

// GET /scan-now - Scan SD card for pending folders
void TestWebServer::handleScanNow() {
    LOG("[TestWebServer] SD card scan requested via web interface");
    
    // Set global scan flag
    g_scanNowFlag = true;
    
    // Add CORS headers
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String response = "{\"status\":\"success\",\"message\":\"SD card scan triggered. Refresh page to see updated folder counts.\"}";
    server->send(200, "application/json", response);
}

// GET /status - JSON status information
void TestWebServer::handleStatus() {
    // Add CORS headers
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String json = "{";
    json += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
    json += "\"current_time\":\"" + getCurrentTimeString() + "\",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    
    // WiFi information
    if (wifiManager && wifiManager->isConnected()) {
        json += "\"wifi_connected\":true,";
        json += "\"wifi_rssi\":" + String(wifiManager->getSignalStrength()) + ",";
        json += "\"wifi_quality\":\"" + wifiManager->getSignalQuality() + "\",";
    } else {
        json += "\"wifi_connected\":false,";
    }
    
    if (scheduleManager) {
        json += "\"next_upload_seconds\":" + String(scheduleManager->getSecondsUntilNextUpload()) + ",";
        json += "\"time_synced\":" + String(scheduleManager->isTimeSynced() ? "true" : "false") + ",";
    }
    
    if (budgetManager) {
        json += "\"budget_remaining_ms\":" + String(budgetManager->getRemainingBudgetMs()) + ",";
        json += "\"transfer_rate_bytes_per_sec\":" + String(budgetManager->getTransmissionRate()) + ",";
    }
    
    // Upload progress and retry information
    if (stateManager) {
        int completedFolders = stateManager->getCompletedFoldersCount();
        int incompleteFolders = stateManager->getIncompleteFoldersCount();
        int totalFolders = completedFolders + incompleteFolders;
        
        json += "\"completed_folders\":" + String(completedFolders) + ",";
        json += "\"incomplete_folders\":" + String(incompleteFolders) + ",";
        json += "\"total_folders\":" + String(totalFolders) + ",";
        json += "\"upload_state_initialized\":" + String(totalFolders > 0 ? "true" : "false") + ",";
        
        String retryFolder = stateManager->getCurrentRetryFolder();
        if (!retryFolder.isEmpty()) {
            int retryCount = stateManager->getCurrentRetryCount();
            json += "\"current_retry_folder\":\"" + retryFolder + "\",";
            json += "\"current_retry_count\":" + String(retryCount);
        } else {
            json += "\"current_retry_folder\":null,";
            json += "\"current_retry_count\":0";
        }
    } else {
        json += "\"completed_folders\":0,";
        json += "\"incomplete_folders\":0,";
        json += "\"total_folders\":0,";
        json += "\"upload_state_initialized\":false,";
        json += "\"current_retry_folder\":null,";
        json += "\"current_retry_count\":0";
    }
    
    if (config) {
        json += ",\"endpoint_type\":\"" + config->getEndpointType() + "\"";
        json += ",\"upload_hour\":" + String(config->getUploadHour());
        json += ",\"session_duration_seconds\":" + String(config->getSessionDurationSeconds());
        json += ",\"max_retry_attempts\":" + String(config->getMaxRetryAttempts());
        json += ",\"boot_delay_seconds\":" + String(config->getBootDelaySeconds());
        json += ",\"sd_release_interval_seconds\":" + String(config->getSdReleaseIntervalSeconds());
    }
    
    // Add retry timing information
    if (budgetExhaustedRetry && nextUploadRetryTime > millis()) {
        unsigned long remainingMs = nextUploadRetryTime - millis();
        json += ",\"retry_wait_active\":true";
        json += ",\"retry_wait_remaining_seconds\":" + String(remainingMs / 1000);
    } else {
        json += ",\"retry_wait_active\":false";
    }
    
    // Add recommendations if retries are happening
    if (stateManager) {
        String retryFolder = stateManager->getCurrentRetryFolder();
        if (!retryFolder.isEmpty()) {
            int retryCount = stateManager->getCurrentRetryCount();
            json += ",\"retry_warning\":true";
            
            if (retryCount >= 2 && config) {
                json += ",\"recommendation\":\"Consider increasing SESSION_DURATION_SECONDS (current: " + 
                       String(config->getSessionDurationSeconds()) + "s)\"";
            }
        } else {
            json += ",\"retry_warning\":false";
        }
    }
    
    // Add CPAP monitor data (without usage percentage)
    if (cpapMonitor) {
        json += ",\"cpap_monitor\":{";
        json += "\"interval_minutes\":10";
        json += ",\"data_points\":144";
        json += ",\"usage_data\":" + cpapMonitor->getUsageDataJSON();
        json += "}";
    }

    
    json += "}";
    
    server->send(200, "application/json", json);
}

// GET /reset-state - Clear upload state
void TestWebServer::handleResetState() {
    LOG("[TestWebServer] State reset requested via web interface");
    
    // Set global reset flag
    g_resetStateFlag = true;
    
    // Add CORS headers
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String response = "{\"status\":\"success\",\"message\":\"Upload state will be reset. Check serial output for confirmation.\"}";
    server->send(200, "application/json", response);
}

// GET /config - Display current configuration
void TestWebServer::handleConfig() {
    // Add CORS headers
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String json = "{";
    
    if (config) {
        // Check if credentials are stored in secure mode
        bool credentialsSecured = config->areCredentialsInFlash();
        
        json += "\"wifi_ssid\":\"" + config->getWifiSSID() + "\",";
        
        // Return censored value for WiFi password if in secure mode
        if (credentialsSecured) {
            json += "\"wifi_password\":\"***STORED_IN_FLASH***\",";
        } else {
            json += "\"wifi_password\":\"" + config->getWifiPassword() + "\",";
        }
        
        json += "\"endpoint\":\"" + config->getEndpoint() + "\",";
        json += "\"endpoint_type\":\"" + config->getEndpointType() + "\",";
        json += "\"endpoint_user\":\"" + config->getEndpointUser() + "\",";
        
        // Return censored value for endpoint password if in secure mode
        if (credentialsSecured) {
            json += "\"endpoint_password\":\"***STORED_IN_FLASH***\",";
        } else {
            json += "\"endpoint_password\":\"" + config->getEndpointPassword() + "\",";
        }
        
        json += "\"upload_hour\":" + String(config->getUploadHour()) + ",";
        json += "\"session_duration_seconds\":" + String(config->getSessionDurationSeconds()) + ",";
        json += "\"max_retry_attempts\":" + String(config->getMaxRetryAttempts()) + ",";
        json += "\"gmt_offset_hours\":" + String(config->getGmtOffsetHours()) + ",";
        
        // Add credentials_secured field to indicate storage mode
        json += "\"credentials_secured\":" + String(credentialsSecured ? "true" : "false");
    }
    
    json += "}";
    
    server->send(200, "application/json", json);
}

// Handle 404 errors
void TestWebServer::handleNotFound() {
    String uri = server->uri();
    
    // Silently handle common browser requests that we don't care about
    if (uri == "/favicon.ico" || uri == "/apple-touch-icon.png" || 
        uri == "/apple-touch-icon-precomposed.png" || uri == "/robots.txt") {
        server->send(404, "text/plain", "Not found");
        return;
    }
    
    // Log unexpected 404s
    LOG_DEBUGF("[TestWebServer] 404 Not Found: %s", uri.c_str());
    
    String message = "{\"status\":\"error\",\"message\":\"Endpoint not found\",\"path\":\"" + uri + "\"}";
    server->send(404, "application/json", message);
}

// Helper: Get uptime as formatted string
String TestWebServer::getUptimeString() {
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
String TestWebServer::getCurrentTimeString() {
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
int TestWebServer::getPendingFilesCount() {
    if (!stateManager) {
        return 0;
    }
    
    // Count files in incomplete DATALOG folders
    // Note: This requires SD card access, which we don't have here
    // Return -1 to indicate "unknown" rather than misleading 0
    return -1;
}

// Helper: Get count of pending DATALOG folders
int TestWebServer::getPendingFoldersCount() {
    if (!stateManager) {
        return 0;
    }
    
    // Get count of incomplete folders from state manager
    return stateManager->getIncompleteFoldersCount();
}

// GET /logs - Retrieve system logs from circular buffer
void TestWebServer::handleLogs() {
    // Add CORS headers for cross-origin access
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    // Retrieve logs from Logger
    Logger::LogData logData = Logger::getInstance().retrieveLogs();
    
    // Build JSON response
    String json = "{";
    json += "\"status\":\"success\",";
    json += "\"logs\":\"" + escapeJson(logData.content) + "\",";
    json += "\"bytes_lost\":" + String(logData.bytesLost) + ",";
    json += "\"bytes_returned\":" + String(logData.content.length()) + ",";
    json += "\"timestamp\":\"" + getCurrentTimeString() + "\"";
    json += "}";
    
    server->send(200, "application/json", json);
}

// Update manager references (needed after uploader recreation)
void TestWebServer::updateManagers(UploadStateManager* state, TimeBudgetManager* budget, ScheduleManager* schedule) {
    stateManager = state;
    budgetManager = budget;
    scheduleManager = schedule;
}

// Set WiFi manager reference
void TestWebServer::setWiFiManager(WiFiManager* wifi) {
    wifiManager = wifi;
}

// Helper: Escape special characters for JSON string
String TestWebServer::escapeJson(const String& str) {
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
