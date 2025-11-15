#include "UploadStateManager.h"
#include <ArduinoJson.h>

#ifdef UNIT_TEST
#include "MockMD5.h"
#else
#include "esp32/rom/md5_hash.h"
#endif

UploadStateManager::UploadStateManager() 
    : stateFilePath("/.upload_state.json"),
      lastUploadTimestamp(0),
      currentRetryCount(0) {
}

bool UploadStateManager::begin(fs::FS &sd) {
    Serial.println("[UploadStateManager] Initializing...");
    
    // Try to load existing state
    if (!loadState(sd)) {
        Serial.println("[UploadStateManager] WARNING: No existing state file or failed to load");
        Serial.println("[UploadStateManager] Starting with empty state - all files will be considered new");
        
        // Initialize with empty state - this is safe and allows operation to continue
        fileChecksums.clear();
        completedDatalogFolders.clear();
        currentRetryFolder = "";
        currentRetryCount = 0;
        lastUploadTimestamp = 0;
    }
    
    return true;  // Always return true - we can operate with empty state
}

String UploadStateManager::calculateChecksum(fs::FS &sd, const String& filePath) {
    File file = sd.open(filePath, FILE_READ);
    if (!file) {
        Serial.print("[UploadStateManager] ERROR: Failed to open file for checksum: ");
        Serial.println(filePath);
        return "";
    }
    
    // Check if file is readable
    if (!file.available() && file.size() > 0) {
        Serial.print("[UploadStateManager] ERROR: File exists but cannot be read: ");
        Serial.println(filePath);
        file.close();
        return "";
    }
    
    struct MD5Context md5_ctx;
    MD5Init(&md5_ctx);
    
    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    size_t totalBytesRead = 0;
    size_t expectedSize = file.size();
    
    while (file.available()) {
        size_t bytesRead = file.read(buffer, bufferSize);
        if (bytesRead == 0) {
            // Read error
            Serial.print("[UploadStateManager] ERROR: Read error while calculating checksum for: ");
            Serial.println(filePath);
            file.close();
            return "";
        }
        
        MD5Update(&md5_ctx, buffer, bytesRead);
        totalBytesRead += bytesRead;
        
        // Yield periodically to prevent watchdog timeout on large files
        if (totalBytesRead % (10 * bufferSize) == 0) {
            yield();
        }
    }
    
    // Verify we read the expected amount
    if (totalBytesRead != expectedSize) {
        Serial.print("[UploadStateManager] WARNING: Checksum size mismatch for ");
        Serial.print(filePath);
        Serial.print(" (read ");
        Serial.print(totalBytesRead);
        Serial.print(" bytes, expected ");
        Serial.print(expectedSize);
        Serial.println(" bytes)");
    }
    
    uint8_t hash[16];
    MD5Final(hash, &md5_ctx);
    
    file.close();
    
    // Convert hash to hex string
    String checksumStr = "";
    for (int i = 0; i < 16; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        checksumStr += hex;
    }
    
    return checksumStr;
}

bool UploadStateManager::hasFileChanged(fs::FS &sd, const String& filePath) {
    // Calculate current checksum
    String currentChecksum = calculateChecksum(sd, filePath);
    if (currentChecksum.isEmpty()) {
        // File doesn't exist or can't be read
        return false;
    }
    
    // Check if we have a stored checksum
    auto it = fileChecksums.find(filePath);
    if (it == fileChecksums.end()) {
        // No stored checksum, file is new
        return true;
    }
    
    // Compare checksums
    return (it->second != currentChecksum);
}

void UploadStateManager::markFileUploaded(const String& filePath, const String& checksum) {
    fileChecksums[filePath] = checksum;
}

bool UploadStateManager::isFolderCompleted(const String& folderName) {
    return completedDatalogFolders.find(folderName) != completedDatalogFolders.end();
}

void UploadStateManager::markFolderCompleted(const String& folderName) {
    completedDatalogFolders.insert(folderName);
    
    // Clear retry tracking for this folder since it's now complete
    if (currentRetryFolder == folderName) {
        clearCurrentRetry();
    }
}

int UploadStateManager::getCurrentRetryCount() {
    return currentRetryCount;
}

void UploadStateManager::setCurrentRetryFolder(const String& folderName) {
    if (currentRetryFolder != folderName) {
        // New folder, reset count
        currentRetryFolder = folderName;
        currentRetryCount = 0;
    }
}

void UploadStateManager::incrementCurrentRetryCount() {
    currentRetryCount++;
}

void UploadStateManager::clearCurrentRetry() {
    currentRetryFolder = "";
    currentRetryCount = 0;
}

unsigned long UploadStateManager::getLastUploadTimestamp() {
    return lastUploadTimestamp;
}

void UploadStateManager::setLastUploadTimestamp(unsigned long timestamp) {
    lastUploadTimestamp = timestamp;
}

bool UploadStateManager::save(fs::FS &sd) {
    return saveState(sd);
}

bool UploadStateManager::loadState(fs::FS &sd) {
    File file = sd.open(stateFilePath, FILE_READ);
    if (!file) {
        Serial.println("[UploadStateManager] State file does not exist - will create on first save");
        return false;
    }
    
    // Check file size to detect corruption
    size_t fileSize = file.size();
    if (fileSize == 0) {
        Serial.println("[UploadStateManager] WARNING: State file is empty (corrupted)");
        file.close();
        return false;
    }
    
    if (fileSize > 65536) {  // 64KB sanity check
        Serial.print("[UploadStateManager] WARNING: State file unusually large (");
        Serial.print(fileSize);
        Serial.println(" bytes) - may be corrupted");
        file.close();
        return false;
    }
    
    // Allocate JSON document
    DynamicJsonDocument doc(4096);
    
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("[UploadStateManager] ERROR: Failed to parse state file: ");
        Serial.println(error.c_str());
        Serial.println("[UploadStateManager] State file may be corrupted - continuing with empty state");
        return false;
    }
    
    // Validate version field
    int version = doc["version"] | 0;
    if (version != 1) {
        Serial.print("[UploadStateManager] WARNING: Unknown state file version: ");
        Serial.println(version);
        Serial.println("[UploadStateManager] Attempting to load anyway...");
    }
    
    // Load timestamp
    lastUploadTimestamp = doc["last_upload_timestamp"] | 0UL;
    
    // Load file checksums
    fileChecksums.clear();
    JsonObject checksums = doc.getObject("file_checksums");
    if (!checksums.isNull()) {
        for (auto& kv : *checksums.data) {
            fileChecksums[String(kv.first.c_str())] = String(kv.second.as<const char*>());
        }
    }
    
    // Load completed folders
    completedDatalogFolders.clear();
    JsonArray folders = doc.getArray("completed_datalog_folders");
    if (!folders.isNull()) {
        for (const auto& v : *folders.data) {
            completedDatalogFolders.insert(String(v.as<const char*>()));
        }
    }
    
    // Load retry tracking
    currentRetryFolder = doc["current_retry_folder"] | "";
    currentRetryCount = doc["current_retry_count"] | 0;
    
    Serial.println("[UploadStateManager] State file loaded successfully");
    Serial.print("[UploadStateManager]   Tracked files: ");
    Serial.println(fileChecksums.size());
    Serial.print("[UploadStateManager]   Completed folders: ");
    Serial.println(completedDatalogFolders.size());
    if (!currentRetryFolder.isEmpty()) {
        Serial.print("[UploadStateManager]   Current retry folder: ");
        Serial.print(currentRetryFolder);
        Serial.print(" (attempt ");
        Serial.print(currentRetryCount);
        Serial.println(")");
    }
    
    return true;
}

bool UploadStateManager::saveState(fs::FS &sd) {
    // Allocate JSON document
    DynamicJsonDocument doc(4096);
    
    // Save version
    doc["version"] = 1;
    
    // Save timestamp
    doc["last_upload_timestamp"] = lastUploadTimestamp;
    
    // Save file checksums
    JsonObject checksums = doc.createNestedObject("file_checksums");
    for (const auto& pair : fileChecksums) {
        checksums[pair.first.c_str()] = JsonVariant(pair.second.c_str());
    }
    
    // Save completed folders
    JsonArray folders = doc.createNestedArray("completed_datalog_folders");
    for (const String& folder : completedDatalogFolders) {
        folders.add(folder);
    }
    
    // Save retry tracking
    doc["current_retry_folder"] = JsonVariant(currentRetryFolder.c_str());
    doc["current_retry_count"] = currentRetryCount;
    
    // Write to temporary file first to avoid corruption
    String tempFilePath = stateFilePath + ".tmp";
    File file = sd.open(tempFilePath, FILE_WRITE);
    if (!file) {
        Serial.print("[UploadStateManager] ERROR: Failed to open state file for writing: ");
        Serial.println(tempFilePath);
        return false;
    }
    
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    if (bytesWritten == 0) {
        Serial.println("[UploadStateManager] ERROR: Failed to write state file (0 bytes written)");
        sd.remove(tempFilePath);  // Clean up failed temp file
        return false;
    }
    
    // Verify the temp file was written correctly
    File verifyFile = sd.open(tempFilePath, FILE_READ);
    if (!verifyFile) {
        Serial.println("[UploadStateManager] ERROR: Failed to verify temp state file");
        sd.remove(tempFilePath);
        return false;
    }
    
    size_t verifySize = verifyFile.size();
    verifyFile.close();
    
    if (verifySize != bytesWritten) {
        Serial.print("[UploadStateManager] ERROR: State file size mismatch (wrote ");
        Serial.print(bytesWritten);
        Serial.print(" bytes, file is ");
        Serial.print(verifySize);
        Serial.println(" bytes)");
        sd.remove(tempFilePath);
        return false;
    }
    
    // Remove old state file if it exists
    if (sd.exists(stateFilePath)) {
        if (!sd.remove(stateFilePath)) {
            Serial.println("[UploadStateManager] WARNING: Failed to remove old state file");
            // Continue anyway - rename might still work
        }
    }
    
    // Rename temp file to actual state file
    if (!sd.rename(tempFilePath, stateFilePath)) {
        Serial.println("[UploadStateManager] ERROR: Failed to rename temp state file");
        sd.remove(tempFilePath);  // Clean up
        return false;
    }
    
    Serial.print("[UploadStateManager] State file saved successfully (");
    Serial.print(bytesWritten);
    Serial.println(" bytes)");
    return true;
}
