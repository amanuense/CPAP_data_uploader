#include "CPAPMonitor.h"

#ifdef ENABLE_TEST_WEBSERVER

#include "Logger.h"
#include "pins_config.h"
#include <time.h>

CPAPMonitor::CPAPMonitor() 
    : lastCheckTime(0)
    , currentIndex(0)
    , initialized(false)
{
    // Initialize all usage data to -1 (not checked yet)
    for (int i = 0; i < INTERVALS_PER_DAY; i++) {
        usageData[i] = -1;
    }
}

void CPAPMonitor::begin() {
    LOG_DEBUG("[CPAPMonitor] Initializing CPAP SD card usage monitor");
    LOG_DEBUGF("[CPAPMonitor] Monitoring interval: %d minutes", INTERVAL_MINUTES);
    LOG_DEBUGF("[CPAPMonitor] Data retention: 24 hours (%d intervals)", INTERVALS_PER_DAY);
    
    initialized = true;
    lastCheckTime = millis();
}

int CPAPMonitor::getIntervalIndex() const {
    // Get current time
    time_t now = time(nullptr);
    
    // Check if time is synchronized
    if (now < 946684800) {  // Before Jan 1, 2000
        // Time not synced, use millis-based index
        unsigned long minutesSinceBoot = millis() / 60000;
        return (minutesSinceBoot / INTERVAL_MINUTES) % INTERVALS_PER_DAY;
    }
    
    // Use actual time to calculate index
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
        // Fallback to millis-based index
        unsigned long minutesSinceBoot = millis() / 60000;
        return (minutesSinceBoot / INTERVAL_MINUTES) % INTERVALS_PER_DAY;
    }
    
    // Calculate minutes since midnight
    int minutesSinceMidnight = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    // Calculate interval index (0-143)
    return (minutesSinceMidnight / INTERVAL_MINUTES) % INTERVALS_PER_DAY;
}

void CPAPMonitor::update() {
    if (!initialized) {
        return;
    }
    
    // Check if it's time for the next check (every 10 minutes)
    unsigned long currentTime = millis();
    unsigned long intervalMs = INTERVAL_MINUTES * 60 * 1000;
    
    if (currentTime - lastCheckTime < intervalMs) {
        return;  // Not time yet
    }
    
    // Update last check time
    lastCheckTime = currentTime;
    
    // Get current interval index
    int newIndex = getIntervalIndex();
    
    // Check if CPAP is using SD card (CS_SENSE pin LOW = in use)
    bool cpapUsing = (digitalRead(CS_SENSE) == LOW);
    
    // Store the data at the current index (1 = using, 0 = available)
    usageData[newIndex] = cpapUsing ? 1 : 0;
    currentIndex = newIndex;
    
    LOG_DEBUGF("[CPAPMonitor] Interval %d: CPAP %s SD card", 
               newIndex, cpapUsing ? "USING" : "NOT USING");
}

int8_t CPAPMonitor::getUsageStatus(int minutesAgo) const {
    if (!initialized || minutesAgo < 0) {
        return -1;
    }
    
    // Calculate how many intervals ago
    int intervalsAgo = minutesAgo / INTERVAL_MINUTES;
    
    if (intervalsAgo >= INTERVALS_PER_DAY) {
        return -1;  // Too far back
    }
    
    // Calculate the index
    int index = (currentIndex - intervalsAgo + INTERVALS_PER_DAY) % INTERVALS_PER_DAY;
    
    return usageData[index];
}

int CPAPMonitor::getUsagePercentage() const {
    if (!initialized) {
        return 0;
    }
    
    int usageCount = 0;
    int validCount = 0;
    
    for (int i = 0; i < INTERVALS_PER_DAY; i++) {
        if (usageData[i] >= 0) {  // Only count checked intervals
            validCount++;
            if (usageData[i] == 1) {
                usageCount++;
            }
        }
    }
    
    if (validCount == 0) {
        return 0;
    }
    
    return (usageCount * 100) / validCount;
}

String CPAPMonitor::getUsageDataJSON() const {
    String json = "[";
    
    // Return last 24 hours of data, starting from oldest to newest
    for (int i = 0; i < INTERVALS_PER_DAY; i++) {
        // Calculate index going backwards from current
        int index = (currentIndex - INTERVALS_PER_DAY + 1 + i + INTERVALS_PER_DAY) % INTERVALS_PER_DAY;
        
        if (i > 0) {
            json += ",";
        }
        
        json += String(usageData[index]);
    }
    
    json += "]";
    return json;
}

String CPAPMonitor::getUsageTableHTML() const {
    String html = "<table style='border-collapse: collapse; width: 100%; margin-top: 10px;'>";
    
    // Header row
    html += "<tr style='background: #f0f0f0;'>";
    html += "<th style='border: 1px solid #ddd; padding: 8px; text-align: left;'>Hour</th>";
    for (int col = 0; col < 6; col++) {
        html += "<th style='border: 1px solid #ddd; padding: 8px; text-align: center;'>";
        html += String(col * 10) + "m";
        html += "</th>";
    }
    html += "</tr>";
    
    // Data rows (24 hours)
    for (int hour = 0; hour < 24; hour++) {
        html += "<tr>";
        html += "<td style='border: 1px solid #ddd; padding: 8px; font-weight: bold;'>";
        html += String(hour) + ":00";
        html += "</td>";
        
        // 6 columns per hour (every 10 minutes)
        for (int col = 0; col < 6; col++) {
            int intervalIndex = hour * 6 + col;
            int8_t status = usageData[intervalIndex];
            
            html += "<td style='border: 1px solid #ddd; padding: 8px; text-align: center;";
            
            // Color code based on status
            if (status == -1) {
                html += " background: #e0e0e0; color: #999;'>-";  // Not checked yet
            } else if (status == 1) {
                html += " background: #ffcccc; color: #cc0000;'>&#9679;";  // CPAP using (filled circle)
            } else {
                html += " background: #ccffcc; color: #00cc00;'>&#9675;";  // Available (empty circle)
            }
            
            html += "</td>";
        }
        
        html += "</tr>";
    }
    
    html += "</table>";
    
    // Legend
    html += "<div style='margin-top: 10px; font-size: 12px;'>";
    html += "<span style='color: #00cc00;'>&#9675; Available</span> | ";
    html += "<span style='color: #cc0000;'>&#9679; CPAP Using</span> | ";
    html += "<span style='color: #999;'>- Not Checked</span>";
    html += "</div>";
    
    return html;
}

#endif // ENABLE_TEST_WEBSERVER
