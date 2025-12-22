#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <Update.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

class OTAManager {
private:
    bool updateInProgress;
    size_t totalSize;
    size_t writtenSize;
    String currentVersion;
    
    // Progress callback function pointer
    typedef void (*ProgressCallback)(size_t written, size_t total);
    ProgressCallback progressCallback;
    
    // Validation helpers
    bool validateFirmware(const uint8_t* data, size_t length);
    bool isValidFirmwareHeader(const uint8_t* data);

public:
    OTAManager();
    
    // Initialize OTA manager
    bool begin();
    
    // Set version string
    void setCurrentVersion(const String& version);
    String getCurrentVersion() const;
    
    // Set progress callback
    void setProgressCallback(ProgressCallback callback);
    
    // Manual OTA methods
    bool startUpdate(size_t firmwareSize);
    bool writeChunk(const uint8_t* data, size_t length);
    bool finishUpdate();
    void abortUpdate();
    void forceReset();
    
    // Download and install from URL
    bool updateFromURL(const String& url);
    
    // Status methods
    bool isUpdateInProgress() const;
    float getProgress() const;  // Returns 0.0 to 100.0
    size_t getBytesWritten() const;
    size_t getTotalSize() const;
    
    // Get last error
    String getLastError() const;
};

#endif // OTA_MANAGER_H