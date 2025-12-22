#include "OTAManager.h"
#include "Logger.h"
#include <esp_ota_ops.h>

OTAManager::OTAManager() 
    : updateInProgress(false), 
      totalSize(0), 
      writtenSize(0),
      currentVersion("unknown"),
      progressCallback(nullptr) {
}

bool OTAManager::begin() {
    LOG("[OTA] Initializing OTA Manager...");
    
    // Get current version from partition info
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        LOG_DEBUGF("[OTA] Running from partition: %s", running->label);
    }
    
    LOG("[OTA] OTA Manager initialized successfully");
    return true;
}

void OTAManager::setCurrentVersion(const String& version) {
    currentVersion = version;
    LOG_DEBUGF("[OTA] Current version set to: %s", version.c_str());
}

String OTAManager::getCurrentVersion() const {
    return currentVersion;
}

void OTAManager::setProgressCallback(ProgressCallback callback) {
    progressCallback = callback;
}

bool OTAManager::isValidFirmwareHeader(const uint8_t* data) {
    // ESP32 firmware starts with 0xE9 magic byte
    if (data[0] != 0xE9) {
        LOG_ERROR("[OTA] Invalid firmware: Missing ESP32 magic byte");
        return false;
    }
    
    // Basic sanity checks for ESP32 firmware header
    // Bytes 1-3 contain segment count and other header info
    if (data[1] > 16) {  // Reasonable segment count limit
        LOG_ERROR("[OTA] Invalid firmware: Suspicious segment count");
        return false;
    }
    
    return true;
}

bool OTAManager::validateFirmware(const uint8_t* data, size_t length) {
    if (length < 32) {
        LOG_ERROR("[OTA] Firmware too small for validation");
        return false;
    }
    
    if (!isValidFirmwareHeader(data)) {
        return false;
    }
    
    LOG_DEBUG("[OTA] Firmware validation passed");
    return true;
}

bool OTAManager::startUpdate(size_t firmwareSize) {
    if (updateInProgress) {
        LOG_ERROR("[OTA] Update already in progress");
        return false;
    }
    
    // Allow size 0 for chunked uploads - we'll determine the actual size later
    if (firmwareSize > 0x180000) {  // Max 1.5MB firmware (OTA partition size)
        LOG_ERROR("[OTA] Firmware size too large");
        return false;
    }
    
    LOG_DEBUGF("[OTA] Starting update, firmware size: %u bytes", firmwareSize);
    
    // For chunked uploads (size 0), use the OTA partition size as default
    size_t updateSize = (firmwareSize == 0) ? 0x180000 : firmwareSize;  // 1.5MB default
    
    if (!Update.begin(updateSize)) {
        LOG_ERROR("[OTA] Failed to begin update");
        LOG_DEBUGF("[OTA] Update error: %s", Update.errorString());
        return false;
    }
    
    updateInProgress = true;
    totalSize = firmwareSize;  // Keep original size (may be 0)
    writtenSize = 0;
    
    LOG("[OTA] Update started successfully");
    return true;
}

bool OTAManager::writeChunk(const uint8_t* data, size_t length) {
    if (!updateInProgress) {
        LOG_ERROR("[OTA] No update in progress");
        return false;
    }
    
    // Validate first chunk (firmware header)
    if (writtenSize == 0 && !validateFirmware(data, length)) {
        abortUpdate();
        return false;
    }
    
    size_t written = Update.write(const_cast<uint8_t*>(data), length);
    if (written != length) {
        LOG_ERROR("[OTA] Failed to write chunk");
        LOG_DEBUGF("[OTA] Expected %u bytes, wrote %u bytes", length, written);
        LOG_DEBUGF("[OTA] Update error: %s", Update.errorString());
        abortUpdate();
        return false;
    }
    
    writtenSize += written;
    
    // Call progress callback if set
    if (progressCallback) {
        progressCallback(writtenSize, totalSize);
    }
    
    LOG_DEBUGF("[OTA] Wrote %u bytes, total: %u/%u (%.1f%%)", 
               written, writtenSize, totalSize, getProgress());
    
    return true;
}

bool OTAManager::finishUpdate() {
    if (!updateInProgress) {
        LOG_ERROR("[OTA] No update in progress");
        return false;
    }
    
    // For chunked uploads, we might not know the total size upfront
    if (totalSize > 0 && writtenSize != totalSize) {
        LOG_ERROR("[OTA] Incomplete update");
        LOG_DEBUGF("[OTA] Expected %u bytes, got %u bytes", totalSize, writtenSize);
        abortUpdate();
        return false;
    }
    
    // Update totalSize if it was unknown (chunked upload)
    if (totalSize == 0) {
        totalSize = writtenSize;
        LOG_DEBUGF("[OTA] Final firmware size: %u bytes", totalSize);
    }
    
    if (!Update.end(true)) {
        LOG_ERROR("[OTA] Failed to finish update");
        LOG_DEBUGF("[OTA] Update error: %s", Update.errorString());
        updateInProgress = false;
        return false;
    }
    
    updateInProgress = false;
    LOG("[OTA] Update completed successfully!");
    LOG("[OTA] Device will restart in 3 seconds...");
    
    return true;
}

void OTAManager::abortUpdate() {
    if (updateInProgress) {
        Update.abort();
        updateInProgress = false;
        LOG("[OTA] Update aborted");
    }
}

void OTAManager::forceReset() {
    LOG("[OTA] Force resetting OTA state");
    if (updateInProgress) {
        Update.abort();
    }
    updateInProgress = false;
    totalSize = 0;
    writtenSize = 0;
    LOG("[OTA] OTA state reset complete");
}

bool OTAManager::updateFromURL(const String& url) {
    if (updateInProgress) {
        LOG_ERROR("[OTA] Update already in progress");
        return false;
    }
    
    LOG_DEBUGF("[OTA] Starting download from: %s", url.c_str());
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);  // 30 second timeout
    
    // Add user agent and connection details for better compatibility
    http.addHeader("User-Agent", "ESP32-OTA-Updater/1.0");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    LOG_DEBUG("[OTA] Sending HTTP GET request...");
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        LOG_ERROR("[OTA] HTTP request failed");
        LOG_DEBUGF("[OTA] HTTP code: %d", httpCode);
        
        // Provide more detailed error information
        String errorMsg = "Unknown error";
        switch (httpCode) {
            case HTTPC_ERROR_CONNECTION_REFUSED:
                errorMsg = "Connection refused";
                break;
            case HTTPC_ERROR_SEND_HEADER_FAILED:
                errorMsg = "Send header failed";
                break;
            case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
                errorMsg = "Send payload failed";
                break;
            case HTTPC_ERROR_NOT_CONNECTED:
                errorMsg = "Not connected";
                break;
            case HTTPC_ERROR_CONNECTION_LOST:
                errorMsg = "Connection lost";
                break;
            case HTTPC_ERROR_NO_STREAM:
                errorMsg = "No stream";
                break;
            case HTTPC_ERROR_NO_HTTP_SERVER:
                errorMsg = "No HTTP server";
                break;
            case HTTPC_ERROR_TOO_LESS_RAM:
                errorMsg = "Too less RAM";
                break;
            case HTTPC_ERROR_ENCODING:
                errorMsg = "Encoding error";
                break;
            case HTTPC_ERROR_STREAM_WRITE:
                errorMsg = "Stream write error";
                break;
            case HTTPC_ERROR_READ_TIMEOUT:
                errorMsg = "Read timeout";
                break;
            default:
                if (httpCode > 0) {
                    errorMsg = "HTTP " + String(httpCode);
                } else {
                    errorMsg = "Network error " + String(httpCode);
                }
                break;
        }
        
        LOG_DEBUGF("[OTA] Error details: %s", errorMsg.c_str());
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        LOG_ERROR("[OTA] Invalid content length");
        http.end();
        return false;
    }
    
    LOG_DEBUGF("[OTA] Firmware size: %d bytes", contentLength);
    
    if (!startUpdate(contentLength)) {
        http.end();
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t totalRead = 0;
    
    while (http.connected() && totalRead < contentLength) {
        size_t available = stream->available();
        if (available == 0) {
            delay(10);
            continue;
        }
        
        size_t toRead = min(available, sizeof(buffer));
        size_t bytesRead = stream->readBytes(buffer, toRead);
        
        if (bytesRead == 0) {
            LOG_ERROR("[OTA] Failed to read from stream");
            abortUpdate();
            http.end();
            return false;
        }
        
        if (!writeChunk(buffer, bytesRead)) {
            http.end();
            return false;
        }
        
        totalRead += bytesRead;
        
        // Yield to prevent watchdog timeout
        yield();
    }
    
    http.end();
    
    if (totalRead != contentLength) {
        LOG_ERROR("[OTA] Download incomplete");
        LOG_DEBUGF("[OTA] Expected %d bytes, got %u bytes", contentLength, totalRead);
        abortUpdate();
        return false;
    }
    
    return finishUpdate();
}

bool OTAManager::isUpdateInProgress() const {
    return updateInProgress;
}

float OTAManager::getProgress() const {
    if (totalSize == 0) return 0.0;
    return (float(writtenSize) / float(totalSize)) * 100.0;
}

size_t OTAManager::getBytesWritten() const {
    return writtenSize;
}

size_t OTAManager::getTotalSize() const {
    return totalSize;
}

String OTAManager::getLastError() const {
    if (Update.hasError()) {
        return Update.errorString();
    }
    return "";
}