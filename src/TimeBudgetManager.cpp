#include "TimeBudgetManager.h"

/**
 * Constructor
 * Initializes the TimeBudgetManager with default values
 */
TimeBudgetManager::TimeBudgetManager() 
    : sessionStartTime(0),
      sessionDurationMs(0),
      transmissionRateBytesPerSec(DEFAULT_RATE),
      rateHistoryIndex(0),
      rateHistoryCount(0) {
    // Initialize rate history array
    for (int i = 0; i < RATE_HISTORY_SIZE; i++) {
        rateHistory[i] = 0;
    }
}

/**
 * Start a new upload session with the specified duration
 * @param durationSeconds Session duration in seconds
 */
void TimeBudgetManager::startSession(unsigned long durationSeconds) {
    sessionStartTime = millis();
    sessionDurationMs = durationSeconds * 1000;
}

/**
 * Start a new upload session with retry multiplier
 * @param durationSeconds Base session duration in seconds
 * @param retryMultiplier Multiplier to extend session duration for retries
 */
void TimeBudgetManager::startSession(unsigned long durationSeconds, int retryMultiplier) {
    sessionStartTime = millis();
    sessionDurationMs = durationSeconds * 1000 * retryMultiplier;
}

/**
 * Get remaining time budget in milliseconds
 * @return Remaining budget in ms, or 0 if budget exhausted
 */
unsigned long TimeBudgetManager::getRemainingBudgetMs() {
    unsigned long elapsed = millis() - sessionStartTime;
    
    if (elapsed >= sessionDurationMs) {
        return 0;
    }
    
    return sessionDurationMs - elapsed;
}

/**
 * Check if there is any time budget remaining
 * @return true if budget remains, false otherwise
 */
bool TimeBudgetManager::hasBudget() {
    return getRemainingBudgetMs() > 0;
}

/**
 * Estimate upload time for a file based on current transmission rate
 * @param fileSize File size in bytes
 * @return Estimated upload time in milliseconds
 */
unsigned long TimeBudgetManager::estimateUploadTimeMs(unsigned long fileSize) {
    // Calculate time = size / rate
    // Convert to milliseconds: (bytes / (bytes/sec)) * 1000
    unsigned long estimatedSeconds = fileSize / transmissionRateBytesPerSec;
    unsigned long remainderMs = ((fileSize % transmissionRateBytesPerSec) * 1000) / transmissionRateBytesPerSec;
    
    return (estimatedSeconds * 1000) + remainderMs;
}

/**
 * Check if a file can be uploaded within remaining budget
 * @param fileSize File size in bytes
 * @return true if file fits in budget, false otherwise
 */
bool TimeBudgetManager::canUploadFile(unsigned long fileSize) {
    unsigned long estimatedTime = estimateUploadTimeMs(fileSize);
    unsigned long remainingBudget = getRemainingBudgetMs();
    
    // Log estimation details for debugging
    #ifdef ENABLE_VERBOSE_LOGGING
    Serial.printf("[Budget] File size: %lu bytes, Estimated time: %lu ms, Remaining: %lu ms, Rate: %lu B/s\n",
                  fileSize, estimatedTime, remainingBudget, transmissionRateBytesPerSec);
    #endif
    
    return estimatedTime <= remainingBudget;
}

/**
 * Record a completed upload to update transmission rate
 * @param fileSize Number of bytes transferred
 * @param elapsedMs Time taken for upload in milliseconds
 */
void TimeBudgetManager::recordUpload(unsigned long fileSize, unsigned long elapsedMs) {
    if (elapsedMs == 0) {
        return; // Avoid division by zero
    }
    
    updateTransmissionRate(fileSize, elapsedMs);
}

/**
 * Update transmission rate with new measurement
 * @param bytesTransferred Number of bytes transferred
 * @param elapsedMs Time taken in milliseconds
 */
void TimeBudgetManager::updateTransmissionRate(unsigned long bytesTransferred, unsigned long elapsedMs) {
    // Calculate rate in bytes per second
    unsigned long rateBytes = (bytesTransferred * 1000) / elapsedMs;
    
    // Add to circular buffer
    rateHistory[rateHistoryIndex] = rateBytes;
    rateHistoryIndex = (rateHistoryIndex + 1) % RATE_HISTORY_SIZE;
    
    if (rateHistoryCount < RATE_HISTORY_SIZE) {
        rateHistoryCount++;
    }
    
    // Update current transmission rate with average
    transmissionRateBytesPerSec = calculateAverageRate();
}

/**
 * Calculate average transmission rate from history
 * @return Average rate in bytes per second
 */
unsigned long TimeBudgetManager::calculateAverageRate() {
    if (rateHistoryCount == 0) {
        return DEFAULT_RATE;
    }
    
    unsigned long sum = 0;
    for (int i = 0; i < rateHistoryCount; i++) {
        sum += rateHistory[i];
    }
    
    return sum / rateHistoryCount;
}

/**
 * Get current transmission rate
 * @return Current transmission rate in bytes per second
 */
unsigned long TimeBudgetManager::getTransmissionRate() {
    return transmissionRateBytesPerSec;
}

/**
 * Get wait time before next session (5 minutes for retry attempts)
 * @return Wait time in milliseconds
 */
unsigned long TimeBudgetManager::getWaitTimeMs() {
    // Wait 5 minutes between retry attempts to give CPAP machine priority
    return 5 * 60 * 1000;  // 5 minutes in milliseconds
}
