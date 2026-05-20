#ifndef TRAFFIC_MONITOR_H
#define TRAFFIC_MONITOR_H

#include <Arduino.h>

/**
 * TrafficMonitor - PCNT-based SD bus activity detector
 * 
 * Uses the ESP32 PCNT (Pulse Counter) peripheral to detect activity on the
 * CS_SENSE pin (GPIO 33). This pin is connected to the SD card's DAT3/CS line
 * on the host (CPAP) side of the bus multiplexer.
 * 
 * When the CPAP machine accesses the SD card, DAT3 toggles at MHz speeds.
 * PCNT counts these edges in hardware — no CPU overhead. The firmware samples
 * the counter periodically (every ~100ms) to determine if the bus is active.
 * 
 * Used by the upload FSM to confirm bus silence before taking SD card control.
 * Also provides a rolling sample buffer for the web-based SD Activity Monitor.
 */

struct ActivitySample {
    uint32_t timestamp;    // millis() / 1000 (seconds since boot)
    uint16_t pulseCount;   // PCNT count for this 1-second window
    bool active;           // pulseCount > 0
};

class TrafficMonitor {
public:
    TrafficMonitor();

    // Singleton access — wraps the global instance declared in main.cpp
    static TrafficMonitor& getInstance();

    void begin(int pin);              // Initialize PCNT on given GPIO
    void update();                    // Call every loop() — non-blocking ~100ms sample
    
    // Activity detection
    bool isBusy();                    // True if activity detected in last sample window
    bool isIdleFor(uint32_t ms);      // True if no activity for at least ms milliseconds
    uint32_t getConsecutiveIdleMs();  // How long has the bus been silent?
    void resetIdleTracking();         // Reset silence counter (e.g., on state transition)
    
    // Sample buffer for SD Activity Monitor web UI
    static const int MAX_SAMPLES = 300;  // 5 minutes at 1 sample/sec
    const ActivitySample* getSampleBuffer() const;
    int getSampleCount() const;
    int getSampleHead() const;        // Circular buffer head index
    
    // Statistics
    uint32_t getLongestIdleMs() const;
    uint32_t getTotalActiveSamples() const;
    uint32_t getTotalIdleSamples() const;
    uint16_t getLastPulseCount() const;
    
    // Reset statistics (e.g., when entering MONITORING mode)
    void resetStatistics();

    // Session-level upload stats — accumulated by the transport layer
    void addTxBytes(unsigned long n) { _totalTxBytes += n; }
    void addFile()                    { _filesUploaded++; }
    unsigned long getTotalTxBytes() const { return _totalTxBytes; }
    int getFilesUploaded() const          { return _filesUploaded; }
    void resetSessionStats()              { _totalTxBytes = 0; _filesUploaded = 0; }

private:
    int _pin;
    bool _initialized;
    
    // 100ms sampling
    unsigned long _lastSampleTime;
    static const uint32_t SAMPLE_INTERVAL_MS = 100;
    bool _lastSampleActive;
    uint16_t _lastPulseCount;
    
    // Idle tracking
    uint32_t _consecutiveIdleMs;
    
    // 1-second aggregation for sample buffer
    unsigned long _lastSecondTime;
    uint32_t _secondPulseAccumulator;
    
    // Circular sample buffer
    ActivitySample _sampleBuffer[MAX_SAMPLES];
    int _sampleHead;
    int _sampleCount;
    
    // Statistics
    uint32_t _longestIdleMs;
    uint32_t _totalActiveSamples;
    uint32_t _totalIdleSamples;

    // Session-level upload stats
    unsigned long _totalTxBytes = 0;
    int _filesUploaded = 0;
    
    void pushSample(uint32_t timestamp, uint16_t pulseCount);
};

#endif // TRAFFIC_MONITOR_H
