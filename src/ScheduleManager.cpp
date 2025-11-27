#include "ScheduleManager.h"
#include "Logger.h"
#include <ESP32Ping.h>

ScheduleManager::ScheduleManager() :
    uploadHour(12),
    lastUploadTimestamp(0),
    ntpSynced(false),
    ntpServer("pool.ntp.org"),
    gmtOffsetHours(0)
{}

bool ScheduleManager::begin(int uploadHour, int gmtOffsetHours) {
    this->uploadHour = uploadHour;
    this->gmtOffsetHours = gmtOffsetHours;
    
    // Validate upload hour
    if (uploadHour < 0 || uploadHour > 23) {
        LOG("Invalid upload hour, using default (12)");
        this->uploadHour = 12;
    }
    
    // Attempt to synchronize time with NTP server
    syncTime();
    
    return true;
}

bool ScheduleManager::syncTime() {
    LOGF("[NTP] Starting time sync with server: %s", ntpServer);
    LOGF("[NTP] GMT offset: %d hours", gmtOffsetHours);
    
    // Allow network to stabilize after WiFi connection
    LOG("[NTP] Waiting 5 seconds for network to stabilize...");
    delay(5000);
    
    // First, check if we can reach the NTP server via ping
    LOG("[NTP] Testing connectivity to NTP server...");
    bool pingSuccess = Ping.ping(ntpServer, 3);  // 3 ping attempts
    
    if (pingSuccess) {
        float avgTime = Ping.averageTime();
        // Only log if we got a valid average time
        if (avgTime > 0 && avgTime < 10000) {  // Sanity check: 0-10 seconds
            LOGF("[NTP] Ping successful! Average time: %.1f ms", avgTime);
        } else {
            LOG("[NTP] Ping reported success but with invalid timing");
        }
    } else {
        LOG("[NTP] WARNING: Cannot ping NTP server (ICMP may be blocked)");
        LOG("[NTP] This is normal for many networks - NTP uses UDP, not ICMP");
        LOG("[NTP] Proceeding with NTP sync...");
    }
    
    // Configure time with NTP server and timezone offset (convert hours to seconds)
    long gmtOffsetSeconds = gmtOffsetHours * 3600L;
    configTime(gmtOffsetSeconds, 0, ntpServer);
    
    // Wait for time to be set (with timeout)
    int retries = 0;
    const int maxRetries = 20;  // Increased timeout
    
    LOG("[NTP] Waiting for time synchronization...");
    while (retries < maxRetries) {
        time_t now = time(nullptr);
        LOGF("[NTP] Retry %d/%d: Current timestamp: %lu", retries + 1, maxRetries, (unsigned long)now);
        
        if (now > 24 * 3600) {  // Time is set if it's past Jan 1, 1970 + 1 day
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                ntpSynced = true;
                LOG("[NTP] Time synchronized successfully!");
                LOGF("[NTP] Current time: %04d-%02d-%02d %02d:%02d:%02d", 
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                return true;
            } else {
                LOG("[NTP] WARNING: Timestamp valid but getLocalTime failed");
            }
        }
        delay(1000);  // Increased from 500ms to 1000ms for high-latency networks
        retries++;
    }
    
    LOG("[NTP] ERROR: Failed to sync time after maximum retries");
    LOG("[NTP] Possible causes:");
    LOG("[NTP]   - Network firewall blocking NTP (UDP port 123)");
    LOG("[NTP]   - DNS resolution failure for pool.ntp.org");
    LOG("[NTP]   - No internet connectivity");
    LOG("[NTP]   - NTP server unreachable from this network");
    ntpSynced = false;
    return false;
}

bool ScheduleManager::isUploadTime() {
    if (!ntpSynced) {
        LOG("Time not synced, cannot check upload schedule");
        return false;
    }
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        LOG("Failed to get local time");
        return false;
    }
    
    // Check if we're in the upload hour
    if (timeinfo.tm_hour != uploadHour) {
        return false;
    }
    
    // Check if we've already uploaded today
    // Convert lastUploadTimestamp to tm struct
    if (lastUploadTimestamp > 0) {
        time_t lastUpload = lastUploadTimestamp;
        struct tm lastUploadInfo;
        localtime_r(&lastUpload, &lastUploadInfo);
        
        // If we uploaded today already, don't upload again
        if (timeinfo.tm_year == lastUploadInfo.tm_year &&
            timeinfo.tm_mon == lastUploadInfo.tm_mon &&
            timeinfo.tm_mday == lastUploadInfo.tm_mday) {
            return false;
        }
    }
    
    return true;
}

void ScheduleManager::markUploadCompleted() {
    lastUploadTimestamp = time(nullptr);
    LOGF("Upload marked as completed at timestamp: %lu", lastUploadTimestamp);
}

unsigned long ScheduleManager::calculateNextUploadTime() {
    if (!ntpSynced) {
        return 0;
    }
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return 0;
    }
    
    // Create a tm struct for the next upload time
    struct tm nextUpload = timeinfo;
    nextUpload.tm_hour = uploadHour;
    nextUpload.tm_min = 0;
    nextUpload.tm_sec = 0;
    
    // If current time is past the upload hour today, schedule for tomorrow
    if (timeinfo.tm_hour >= uploadHour) {
        nextUpload.tm_mday += 1;
    }
    
    // Convert to time_t
    time_t nextUploadTime = mktime(&nextUpload);
    
    return (unsigned long)nextUploadTime;
}

unsigned long ScheduleManager::getSecondsUntilNextUpload() {
    if (!ntpSynced) {
        return 0;
    }
    
    unsigned long nextUploadTime = calculateNextUploadTime();
    time_t now = time(nullptr);
    
    if (nextUploadTime > (unsigned long)now) {
        return nextUploadTime - (unsigned long)now;
    }
    
    return 0;
}

bool ScheduleManager::isTimeSynced() const {
    return ntpSynced;
}

unsigned long ScheduleManager::getLastUploadTimestamp() const {
    return lastUploadTimestamp;
}

void ScheduleManager::setLastUploadTimestamp(unsigned long timestamp) {
    lastUploadTimestamp = timestamp;
}
