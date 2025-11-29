#include "Logger.h"
#include <stdarg.h>

// Singleton instance getter
Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

// Constructor - initialize the logging system
Logger::Logger() 
    : buffer(nullptr)
    , bufferSize(LOG_BUFFER_SIZE)
    , headIndex(0)
    , tailIndex(0)
    , lastReadIndex(0)
    , mutex(nullptr)
    , initialized(false)
{
    // Allocate circular buffer
    buffer = (char*)malloc(bufferSize);
    if (buffer == nullptr) {
        // Memory allocation failed - fall back to serial-only mode
        Serial.println("[LOGGER] ERROR: Failed to allocate circular buffer, falling back to serial-only mode");
        return;
    }

    // Initialize buffer to zeros for cleaner debugging
    memset(buffer, 0, bufferSize);

    // Create FreeRTOS mutex for thread-safe operations
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) {
        // Mutex creation failed - fall back to serial-only mode
        Serial.println("[LOGGER] ERROR: Failed to create mutex, falling back to serial-only mode");
        free(buffer);
        buffer = nullptr;
        return;
    }

    // Initialization successful
    initialized = true;
    #ifdef ENABLE_VERBOSE_LOGGING
    Serial.println("[LOGGER] Initialized successfully with " + String(bufferSize) + " byte circular buffer");
    #endif
}

// Destructor - clean up resources
Logger::~Logger() {
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    
    if (buffer != nullptr) {
        free(buffer);
        buffer = nullptr;
    }
}

// Log a C-string message
void Logger::log(const char* message) {
    if (message == nullptr) {
        return;
    }

    size_t len = strlen(message);
    
    // Write to serial (outside critical section - Serial is thread-safe on ESP32)
    writeToSerial(message, len);
    
    // Write to buffer if initialized
    if (initialized && buffer != nullptr) {
        writeToBuffer(message, len);
    }
}

// Log an Arduino String message
void Logger::log(const String& message) {
    log(message.c_str());
}

// Log a formatted message (printf-style)
void Logger::logf(const char* format, ...) {
    if (format == nullptr) {
        return;
    }

    // Format the message using vsnprintf
    char buffer[256];  // Temporary buffer for formatted message
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Check if message was truncated
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }

    // Log the formatted message
    if (len > 0) {
        log(buffer);
    }
}

// Write data to serial interface
void Logger::writeToSerial(const char* data, size_t len) {
    // Serial.write is thread-safe on ESP32
    Serial.write((const uint8_t*)data, len);
    
    // Add newline if not already present
    if (len > 0 && data[len - 1] != '\n') {
        Serial.write('\n');
    }
}

// Write data to circular buffer with overflow handling
void Logger::writeToBuffer(const char* data, size_t len) {
    if (!initialized || buffer == nullptr || mutex == nullptr) {
        return;
    }

    // Acquire mutex for thread-safe buffer access
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        // Failed to acquire mutex - skip buffer write
        return;
    }

    // Write each byte to the circular buffer
    for (size_t i = 0; i < len; i++) {
        // Calculate physical position in buffer
        size_t physicalPos = headIndex % bufferSize;
        
        // Write byte to buffer
        buffer[physicalPos] = data[i];
        
        // Advance head index (monotonic counter)
        headIndex++;
        
        // Check if we've wrapped around and need to advance tail
        // This happens when we've written more than bufferSize bytes
        if (headIndex - tailIndex > bufferSize) {
            tailIndex = headIndex - bufferSize;
        }
    }

    // Add newline if not already present
    if (len > 0 && data[len - 1] != '\n') {
        size_t physicalPos = headIndex % bufferSize;
        buffer[physicalPos] = '\n';
        headIndex++;
        
        if (headIndex - tailIndex > bufferSize) {
            tailIndex = headIndex - bufferSize;
        }
    }

    // Release mutex
    xSemaphoreGive(mutex);
}

// Calculate number of bytes lost since last read
uint32_t Logger::calculateLostBytes() {
    // If lastReadIndex is behind tailIndex, data was overwritten
    if (lastReadIndex < tailIndex) {
        return tailIndex - lastReadIndex;
    }
    return 0;
}

// Retrieve all logs from buffer and drain it
Logger::LogData Logger::retrieveLogs() {
    LogData result;
    result.bytesLost = 0;

    if (!initialized || buffer == nullptr || mutex == nullptr) {
        // Not initialized - return empty result
        return result;
    }

    // Acquire mutex for thread-safe buffer access
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        // Failed to acquire mutex - return empty result
        return result;
    }

    // Calculate lost bytes before draining
    result.bytesLost = calculateLostBytes();

    // Calculate available data in buffer
    uint32_t availableBytes = headIndex - tailIndex;
    
    // Safety check - should never exceed buffer size
    if (availableBytes > bufferSize) {
        availableBytes = bufferSize;
    }

    // Reserve space in String to avoid multiple reallocations
    result.content.reserve(availableBytes);

    // Read data from tail to head
    for (uint32_t i = 0; i < availableBytes; i++) {
        uint32_t logicalIndex = tailIndex + i;
        size_t physicalPos = logicalIndex % bufferSize;
        result.content += buffer[physicalPos];
    }

    // Update lastReadIndex to current head position
    lastReadIndex = headIndex;
    
    // Drain buffer by moving tail to head (buffer is now empty)
    tailIndex = headIndex;

    // Release mutex
    xSemaphoreGive(mutex);

    return result;
}
