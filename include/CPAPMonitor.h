#ifndef CPAP_MONITOR_H
#define CPAP_MONITOR_H

#include <Arduino.h>

#ifdef ENABLE_TEST_WEBSERVER

/**
 * CPAPMonitor - Monitors CPAP SD card usage patterns
 * 
 * Periodically checks if the CPAP machine is using the SD card
 * and stores the data for 24 hours. Data is stored in 10-minute
 * intervals, creating a 24-hour rolling window of usage patterns.
 */
class CPAPMonitor {
private:
    static const int INTERVAL_MINUTES = 10;
    static const int INTERVALS_PER_DAY = 144;  // 24 hours * 6 intervals per hour
    
    // Usage data: -1 = not checked yet, 0 = available, 1 = CPAP using
    int8_t usageData[INTERVALS_PER_DAY];
    unsigned long lastCheckTime;
    int currentIndex;
    bool initialized;
    
    int getIntervalIndex() const;

public:
    CPAPMonitor();
    
    void begin();
    void update();  // Call this in loop() to check periodically
    
    // Get usage data for web interface
    int8_t getUsageStatus(int minutesAgo) const;  // Returns -1 (not checked), 0 (available), 1 (using)
    int getUsagePercentage() const;  // Percentage of time CPAP was using SD card
    String getUsageDataJSON() const;  // JSON array of last 24 hours
    String getUsageTableHTML() const;  // HTML table for web interface
};

#endif // ENABLE_TEST_WEBSERVER

#endif // CPAP_MONITOR_H
