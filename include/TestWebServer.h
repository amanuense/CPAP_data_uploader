#ifndef TEST_WEB_SERVER_H
#define TEST_WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "Config.h"
#include "UploadStateManager.h"
#include "TimeBudgetManager.h"
#include "ScheduleManager.h"
#include "WiFiManager.h"
#include "CPAPMonitor.h"

#ifdef ENABLE_OTA_UPDATES
#include "OTAManager.h"
#endif

// Global trigger flags for upload and state reset
extern volatile bool g_triggerUploadFlag;
extern volatile bool g_resetStateFlag;
extern volatile bool g_scanNowFlag;

class TestWebServer {
private:
    WebServer* server;
    Config* config;
    UploadStateManager* stateManager;
    TimeBudgetManager* budgetManager;
    ScheduleManager* scheduleManager;
    WiFiManager* wifiManager;
    CPAPMonitor* cpapMonitor;
    
#ifdef ENABLE_OTA_UPDATES
    OTAManager* otaManager;
#endif
    
    // Request handlers
    void handleRoot();
    void handleTriggerUpload();
    void handleScanNow();
    void handleStatus();
    void handleResetState();
    void handleConfig();
    void handleLogs();
    void handleNotFound();
    
#ifdef ENABLE_OTA_UPDATES
    // OTA handlers
    void handleOTAPage();
    void handleOTAUpload();
    void handleOTAUploadComplete();
    void handleOTAURL();
    void handleOTAStatus();
    
    // OTA progress callback
    static void otaProgressCallback(size_t written, size_t total);
#endif
    
    // Helper methods
    String getUptimeString();
    String getCurrentTimeString();
    int getPendingFilesCount();
    int getPendingFoldersCount();
    String escapeJson(const String& str);

public:
    TestWebServer(Config* cfg, UploadStateManager* state, 
                  TimeBudgetManager* budget, ScheduleManager* schedule, 
                  WiFiManager* wifi = nullptr, CPAPMonitor* monitor = nullptr);
    ~TestWebServer();
    
    bool begin();
    void handleClient();
    
    // Update manager references (needed after uploader recreation)
    void updateManagers(UploadStateManager* state, TimeBudgetManager* budget, ScheduleManager* schedule);
    void setWiFiManager(WiFiManager* wifi);
    
#ifdef ENABLE_OTA_UPDATES
    // OTA manager access
    void setOTAManager(OTAManager* ota);
#endif
};

#endif // TEST_WEB_SERVER_H
