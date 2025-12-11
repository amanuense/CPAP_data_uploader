#ifndef CPAP_MONITOR_H
#define CPAP_MONITOR_H

#include <Arduino.h>

#if defined(ENABLE_TEST_WEBSERVER) && defined(ENABLE_CPAP_MONITOR)

/**
 * CPAPMonitor - Monitors CPAP SD card usage patterns
 * 
 * Periodically checks if the CPAP machine is using the SD card
 * and stores the data for 24 hours. Data is stored in 10-minute
 * intervals, creating a 24-hour rolling window of usage patterns.
 * 
 * NOTE: Requires CS_SENSE pin functionality. Disabled by default
 * due to hardware issues with CS_SENSE detection.
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
    String getUsageDataJSON() const;  // JSON array of last 24 hours
    String getUsageTableHTML() const;  // HTML table for web interface
};

#else // !ENABLE_CPAP_MONITOR || !ENABLE_TEST_WEBSERVER

// Stub implementation when CPAP monitoring is disabled
class CPAPMonitor {
public:
    CPAPMonitor() {}
    void begin() {}
    void update() {}
    int8_t getUsageStatus(int minutesAgo) const { return -1; }
    String getUsageDataJSON() const { return "[]"; }
    String getUsageTableHTML() const { return "<p>CPAP monitoring disabled (CS_SENSE hardware issue)</p>"; }
};

#endif // ENABLE_TEST_WEBSERVER && ENABLE_CPAP_MONITOR

#endif // CPAP_MONITOR_H
