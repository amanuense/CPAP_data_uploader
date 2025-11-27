#include "TestWebServer.h"
#include "Logger.h"
#include <time.h>

// Global trigger flags
volatile bool g_triggerUploadFlag = false;
volatile bool g_resetStateFlag = false;

// Constructor
TestWebServer::TestWebServer(Config* cfg, UploadStateManager* state,
                             TimeBudgetManager* budget, ScheduleManager* schedule)
    : server(nullptr),
      config(cfg),
      stateManager(state),
      budgetManager(budget),
      scheduleManager(schedule) {
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
    server->on("/status", [this]() { this->handleStatus(); });
    server->on("/reset-state", [this]() { this->handleResetState(); });
    server->on("/config", [this]() { this->handleConfig(); });
    server->on("/logs", [this]() { this->handleLogs(); });
    server->onNotFound([this]() { this->handleNotFound(); });
    
    // Start the server
    server->begin();
    
    LOG("[TestWebServer] Web server started successfully");
    LOG("[TestWebServer] Available endpoints:");
    LOG("[TestWebServer]   GET /              - Status page (HTML)");
    LOG("[TestWebServer]   GET /trigger-upload - Force immediate upload");
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
    
    html += "<div class='info'><span class='label'>Pending Files:</span><span class='value'>";
    html += String(getPendingFilesCount()) + "</span></div>";
    
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
    
    if (scheduleManager) {
        json += "\"next_upload_seconds\":" + String(scheduleManager->getSecondsUntilNextUpload()) + ",";
        json += "\"time_synced\":" + String(scheduleManager->isTimeSynced() ? "true" : "false") + ",";
    }
    
    if (budgetManager) {
        json += "\"budget_remaining_ms\":" + String(budgetManager->getRemainingBudgetMs()) + ",";
        json += "\"transfer_rate_bytes_per_sec\":" + String(budgetManager->getTransmissionRate()) + ",";
    }
    
    json += "\"pending_files\":" + String(getPendingFilesCount());
    
    if (config) {
        json += ",\"endpoint_type\":\"" + config->getEndpointType() + "\"";
        json += ",\"upload_hour\":" + String(config->getUploadHour());
        json += ",\"session_duration_seconds\":" + String(config->getSessionDurationSeconds());
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
        json += "\"wifi_ssid\":\"" + config->getWifiSSID() + "\",";
        json += "\"endpoint\":\"" + config->getEndpoint() + "\",";
        json += "\"endpoint_type\":\"" + config->getEndpointType() + "\",";
        json += "\"endpoint_user\":\"" + config->getEndpointUser() + "\",";
        json += "\"upload_hour\":" + String(config->getUploadHour()) + ",";
        json += "\"session_duration_seconds\":" + String(config->getSessionDurationSeconds()) + ",";
        json += "\"max_retry_attempts\":" + String(config->getMaxRetryAttempts()) + ",";
        json += "\"gmt_offset_hours\":" + String(config->getGmtOffsetHours());
    }
    
    json += "}";
    
    server->send(200, "application/json", json);
}

// Handle 404 errors
void TestWebServer::handleNotFound() {
    String message = "{\"status\":\"error\",\"message\":\"Endpoint not found\"}";
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
    // This is a simplified estimate
    // In a real implementation, we would scan the SD card
    // For now, return 0 as placeholder
    return 0;
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
