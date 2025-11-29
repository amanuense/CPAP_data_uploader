#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
// Mock FreeRTOS types for native testing
typedef void* SemaphoreHandle_t;
#endif

// Compile-time configuration for circular buffer size
#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE 2048  // Default: 2KB
#endif

// Validate buffer size at compile time
static_assert(LOG_BUFFER_SIZE > 0, "LOG_BUFFER_SIZE must be greater than zero");

/**
 * Logger - Singleton class for dual-output logging system
 * 
 * Provides thread-safe logging to both serial interface and circular RAM buffer.
 * Designed for ESP32 dual-core operation with FreeRTOS mutex protection.
 * 
 * Features:
 * - Dual output: Serial + Circular Buffer
 * - Thread-safe for dual-core ESP32
 * - Automatic buffer overflow handling (overwrites oldest data)
 * - Lost data tracking for buffer overflow scenarios
 * - Configurable buffer size via LOG_BUFFER_SIZE preprocessor definition
 * 
 * Memory Impact:
 * - Buffer: LOG_BUFFER_SIZE bytes (default 2KB)
 * - Overhead: ~32 bytes for state + mutex handle
 * 
 * Configuration:
 * To change buffer size, add to platformio.ini build_flags:
 *   build_flags = -DLOG_BUFFER_SIZE=4096  ; 4KB buffer
 */
class Logger {
public:
    /**
     * Structure returned by retrieveLogs() containing log data and metadata
     */
    struct LogData {
        String content;        // Log content from buffer
        uint32_t bytesLost;    // Number of bytes lost due to overflow since last read
    };

    /**
     * Get singleton instance of Logger
     * Thread-safe initialization on first call
     */
    static Logger& getInstance();

    /**
     * Log a C-string message to both serial and buffer
     * Thread-safe for concurrent calls from multiple cores
     */
    void log(const char* message);

    /**
     * Log an Arduino String message to both serial and buffer
     * Thread-safe for concurrent calls from multiple cores
     */
    void log(const String& message);

    /**
     * Log a formatted message (printf-style) to both serial and buffer
     * Thread-safe for concurrent calls from multiple cores
     * 
     * Example: logf("WiFi connected, IP: %s", ipAddress.c_str());
     */
    void logf(const char* format, ...);

    /**
     * Retrieve all logs from circular buffer and drain the buffer
     * Returns log content and count of bytes lost due to overflow
     * Thread-safe for concurrent access
     * 
     * After this call:
     * - Buffer is emptied (tail = head)
     * - lastReadIndex is updated to current head position
     * - Future calls will only return new logs
     */
    LogData retrieveLogs();

    /**
     * Check if logger is properly initialized
     * Returns false if memory allocation or mutex creation failed
     */
    bool isInitialized() const { return initialized; }

private:
    // Singleton pattern - private constructor/destructor
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * Write data to serial interface
     * Called outside critical section for optimal performance
     */
    void writeToSerial(const char* data, size_t len);

    /**
     * Write data to circular buffer with overflow handling
     * Must be called within mutex protection
     */
    void writeToBuffer(const char* data, size_t len);

    /**
     * Calculate number of bytes lost since last read
     * Returns difference between lastReadIndex and tailIndex if data was overwritten
     */
    uint32_t calculateLostBytes();

    // Circular buffer storage
    char* buffer;
    size_t bufferSize;

    // Monotonic 32-bit indices for circular buffer management
    // These wrap at 2^32 but use modulo arithmetic for physical position
    volatile uint32_t headIndex;      // Next write position (monotonic counter)
    volatile uint32_t tailIndex;      // Oldest valid data position (monotonic counter)
    volatile uint32_t lastReadIndex;  // Last API read position (monotonic counter)

    // Thread safety for dual-core ESP32
    SemaphoreHandle_t mutex;

    // Initialization state
    bool initialized;
};

// Convenience macros for logging

/**
 * Basic logging macro - logs message to both serial and buffer
 * Usage: LOG("System started");
 */
#define LOG(msg) Logger::getInstance().log(msg)

/**
 * Printf-style logging macro with format string
 * Usage: LOGF("Temperature: %d°C", temp);
 */
#define LOGF(fmt, ...) Logger::getInstance().logf(fmt, ##__VA_ARGS__)

/**
 * Level-based logging macros for structured logging
 * These add severity prefixes to messages
 */
#define LOG_INFO(msg) Logger::getInstance().log("[INFO] " msg)
#define LOG_ERROR(msg) Logger::getInstance().log("[ERROR] " msg)
#define LOG_WARN(msg) Logger::getInstance().log("[WARN] " msg)

/**
 * Printf-style level-based logging macros
 * Usage: LOG_INFOF("Connected to %s", ssid);
 */
#define LOG_INFOF(fmt, ...) Logger::getInstance().logf("[INFO] " fmt, ##__VA_ARGS__)
#define LOG_ERRORF(fmt, ...) Logger::getInstance().logf("[ERROR] " fmt, ##__VA_ARGS__)
#define LOG_WARNF(fmt, ...) Logger::getInstance().logf("[WARN] " fmt, ##__VA_ARGS__)

/**
 * Debug logging macros - compiled out unless ENABLE_VERBOSE_LOGGING is defined
 * These are for detailed diagnostics, progress updates, and troubleshooting information
 * that are useful during development but add overhead in production.
 * 
 * To enable: Add -DENABLE_VERBOSE_LOGGING to build_flags in platformio.ini
 * 
 * Usage:
 *   LOG_DEBUG("Detailed operation info");
 *   LOG_DEBUGF("Processing file: %s", filename);
 */
#ifdef ENABLE_VERBOSE_LOGGING
    #define LOG_DEBUG(msg) Logger::getInstance().log("[DEBUG] " msg)
    #define LOG_DEBUGF(fmt, ...) Logger::getInstance().logf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
    // Compile out debug logging - zero overhead
    #define LOG_DEBUG(msg) ((void)0)
    #define LOG_DEBUGF(fmt, ...) ((void)0)
#endif

#endif // LOGGER_H
