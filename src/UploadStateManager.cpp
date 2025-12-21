#include "UploadStateManager.h"
#include "Logger.h"
#include <ArduinoJson.h>

#ifdef UNIT_TEST
#include "MockMD5.h"
#else
#include "esp32/rom/md5_hash.h"
#endif

UploadStateManager::UploadStateManager() 
    : stateFilePath("/.upload_state.json"),
      lastUploadTimestamp(0),
      currentRetryCount(0),
      totalFoldersCount(0) {
}

bool UploadStateManager::begin(fs::FS &sd) {
    LOG("[UploadStateManager] Initializing...");
    
    // Try to load existing state
    if (!loadState(sd)) {
        LOG("[UploadStateManager] WARNING: No existing state file or failed to load");
        LOG("[UploadStateManager] Starting with empty state - all files will be considered new");
        
        // Initialize with empty state - this is safe and allows operation to continue
        fileChecksums.clear();
        completedDatalogFolders.clear();
        pendingDatalogFolders.clear();
        currentRetryFolder = "";
        currentRetryCount = 0;
        lastUploadTimestamp = 0;
    }
    
    return true;  // Always return true - we can operate with empty state
}

String UploadStateManager::calculateChecksum(fs::FS &sd, const String& filePath) {
    File file = sd.open(filePath, FILE_READ);
    if (!file) {
        LOGF("[UploadStateManager] ERROR: Failed to open file for checksum: %s", filePath.c_str());
        return "";
    }
    
    // Check if file is readable
    if (!file.available() && file.size() > 0) {
        LOGF("[UploadStateManager] ERROR: File exists but cannot be read: %s", filePath.c_str());
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
            LOGF("[UploadStateManager] ERROR: Read error while calculating checksum for: %s", filePath.c_str());
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
        LOG_DEBUGF("[UploadStateManager] WARNING: Checksum size mismatch for %s (read %u bytes, expected %u bytes)", 
             filePath.c_str(), totalBytesRead, expectedSize);
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
    
    // Remove from pending state if it was pending
    auto pendingIt = pendingDatalogFolders.find(folderName);
    if (pendingIt != pendingDatalogFolders.end()) {
        pendingDatalogFolders.erase(pendingIt);
        LOG_DEBUGF("[UploadStateManager] Removed folder from pending state: %s", folderName.c_str());
    }
    
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

int UploadStateManager::getCompletedFoldersCount() const {
    return completedDatalogFolders.size();
}

int UploadStateManager::getIncompleteFoldersCount() const {
    if (totalFoldersCount == 0) {
        return 0;  // Not yet scanned
    }
    return totalFoldersCount - completedDatalogFolders.size() - pendingDatalogFolders.size();
}

void UploadStateManager::setTotalFoldersCount(int count) {
    totalFoldersCount = count;
}

bool UploadStateManager::isPendingFolder(const String& folderName) {
    return pendingDatalogFolders.find(folderName) != pendingDatalogFolders.end();
}

void UploadStateManager::markFolderPending(const String& folderName, unsigned long timestamp) {
    pendingDatalogFolders[folderName] = timestamp;
    LOG_DEBUGF("[UploadStateManager] Marked folder as pending: %s (timestamp: %lu)", 
         folderName.c_str(), timestamp);
}

void UploadStateManager::removeFolderFromPending(const String& folderName) {
    auto it = pendingDatalogFolders.find(folderName);
    if (it != pendingDatalogFolders.end()) {
        pendingDatalogFolders.erase(it);
        LOG_DEBUGF("[UploadStateManager] Removed folder from pending state: %s", folderName.c_str());
    }
}

bool UploadStateManager::shouldPromotePendingToCompleted(const String& folderName, unsigned long currentTime) {
    auto it = pendingDatalogFolders.find(folderName);
    if (it == pendingDatalogFolders.end()) {
        return false;  // Not a pending folder
    }
    
    unsigned long firstSeenTime = it->second;
    return (currentTime - firstSeenTime) >= PENDING_FOLDER_TIMEOUT_SECONDS;
}

void UploadStateManager::promotePendingToCompleted(const String& folderName) {
    auto it = pendingDatalogFolders.find(folderName);
    if (it != pendingDatalogFolders.end()) {
        pendingDatalogFolders.erase(it);
        completedDatalogFolders.insert(folderName);
        LOGF("[UploadStateManager] Promoted pending folder to completed: %s (empty for 7+ days)", 
             folderName.c_str());
    }
}

int UploadStateManager::getPendingFoldersCount() const {
    return pendingDatalogFolders.size();
}

String UploadStateManager::getCurrentRetryFolder() const {
    return currentRetryFolder;
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
        LOG("[UploadStateManager] State file does not exist - will create on first save");
        return false;
    }
    
    // Check file size to detect corruption
    size_t fileSize = file.size();
    if (fileSize == 0) {
        LOG("[UploadStateManager] WARNING: State file is empty (corrupted)");
        file.close();
        return false;
    }
    
    if (fileSize > 65536) {  // 64KB sanity check
        LOGF("[UploadStateManager] WARNING: State file unusually large (%u bytes) - may be corrupted", fileSize);
        file.close();
        return false;
    }
    
    // Allocate JSON document with dynamic sizing based on file size
    // ArduinoJson recommends capacity = fileSize * 1.5 to 2.0 for deserialization overhead
    // We use 2.0x multiplier to ensure sufficient capacity for JSON parsing
    size_t jsonCapacity = fileSize * 2;
    
    // Ensure minimum capacity of 4KB for small files
    if (jsonCapacity < 4096) {
        jsonCapacity = 4096;
    }
    
    LOG_DEBUGF("[UploadStateManager] Allocating %u bytes for JSON document (file size: %u bytes)", 
         jsonCapacity, fileSize);
    
    DynamicJsonDocument doc(jsonCapacity);
    
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        LOGF("[UploadStateManager] ERROR: Failed to parse state file: %s", error.c_str());
        
        // Check if error is due to insufficient memory
#ifdef UNIT_TEST
        if (error == DeserializationError(DeserializationError::NoMemory)) {
#else
        if (error == DeserializationError::NoMemory) {
#endif
            LOGF("[UploadStateManager] ERROR: Insufficient memory to parse state file (needed ~%u bytes)", 
                 jsonCapacity);
            LOG("[UploadStateManager] Consider pruning old entries or increasing available RAM");
        }
        
        LOG("[UploadStateManager] State file may be corrupted - continuing with empty state");
        return false;
    }
    
    // Validate version field
    int version = doc["version"] | 0;
    if (version != 1) {
        LOGF("[UploadStateManager] WARNING: Unknown state file version: %d", version);
        LOG("[UploadStateManager] Attempting to load anyway...");
    }
    
    // Load timestamp
    lastUploadTimestamp = doc["last_upload_timestamp"] | 0UL;
    
    // Load file checksums
    fileChecksums.clear();
#ifdef UNIT_TEST
    // Mock ArduinoJson uses getObject() and returns std::map
    JsonObject checksums = doc.getObject("file_checksums");
    if (!checksums.isNull()) {
        for (auto it = checksums.begin(); it != checksums.end(); ++it) {
            fileChecksums[String(it->first.c_str())] = String(it->second.as<const char*>());
        }
    }
#else
    // Real ArduinoJson v6 uses operator[] and JsonPair
    JsonObject checksums = doc["file_checksums"];
    if (!checksums.isNull()) {
        for (JsonPair kv : checksums) {
            fileChecksums[String(kv.key().c_str())] = String(kv.value().as<const char*>());
        }
    }
#endif
    
    // Load completed folders
    completedDatalogFolders.clear();
#ifdef UNIT_TEST
    // Mock ArduinoJson uses getArray()
    JsonArray folders = doc.getArray("completed_datalog_folders");
#else
    // Real ArduinoJson v6 uses operator[] with implicit conversion
    JsonArray folders = doc["completed_datalog_folders"];
#endif
    if (!folders.isNull()) {
        for (JsonVariant v : folders) {
            completedDatalogFolders.insert(String(v.as<const char*>()));
        }
    }
    
    // Load pending folders (backward compatibility - initialize empty if missing)
    pendingDatalogFolders.clear();
#ifdef UNIT_TEST
    // Mock ArduinoJson uses getObject()
    JsonObject pendingFolders = doc.getObject("pending_datalog_folders");
    if (!pendingFolders.isNull()) {
        for (auto it = pendingFolders.begin(); it != pendingFolders.end(); ++it) {
            pendingDatalogFolders[String(it->first.c_str())] = it->second.as<unsigned long>();
        }
    }
#else
    // Real ArduinoJson v6 uses operator[] and JsonPair
    JsonObject pendingFolders = doc["pending_datalog_folders"];
    if (!pendingFolders.isNull()) {
        for (JsonPair kv : pendingFolders) {
            pendingDatalogFolders[String(kv.key().c_str())] = kv.value().as<unsigned long>();
        }
    }
#endif
    
    // Load retry tracking
    currentRetryFolder = doc["current_retry_folder"] | "";
    currentRetryCount = doc["current_retry_count"] | 0;
    
    LOG("[UploadStateManager] State file loaded successfully");
    LOG_DEBUGF("[UploadStateManager]   Tracked files: %u", fileChecksums.size());
    LOG_DEBUGF("[UploadStateManager]   Completed folders: %u", completedDatalogFolders.size());
    LOG_DEBUGF("[UploadStateManager]   Pending folders: %u", pendingDatalogFolders.size());
    if (!currentRetryFolder.isEmpty()) {
        LOG_DEBUGF("[UploadStateManager]   Current retry folder: %s (attempt %d)", 
             currentRetryFolder.c_str(), currentRetryCount);
    }
    
    return true;
}

bool UploadStateManager::saveState(fs::FS &sd) {
    // Calculate required JSON document size dynamically
    // Estimate: base overhead (200) + folders (30 bytes each) + pending folders (50 bytes each) + checksums (100 bytes each)
    size_t estimatedSize = 200 + 
                          (completedDatalogFolders.size() * 30) + 
                          (pendingDatalogFolders.size() * 50) +
                          (fileChecksums.size() * 100);
    
    // Add 50% overhead for JSON formatting and safety margin
    size_t jsonCapacity = estimatedSize * 3 / 2;
    
    // Ensure minimum capacity of 4KB
    if (jsonCapacity < 4096) {
        jsonCapacity = 4096;
    }
    
    LOG_DEBUGF("[UploadStateManager] Allocating %u bytes for JSON document (%u completed, %u pending, %u files)", 
         jsonCapacity, completedDatalogFolders.size(), pendingDatalogFolders.size(), fileChecksums.size());
    
    // Allocate JSON document with calculated capacity
    DynamicJsonDocument doc(jsonCapacity);
    
    // Save version
    doc["version"] = 1;
    
    // Save timestamp
    doc["last_upload_timestamp"] = lastUploadTimestamp;
    
    // Save file checksums
    JsonObject checksums = doc.createNestedObject("file_checksums");
    for (const auto& pair : fileChecksums) {
        checksums[pair.first.c_str()] = pair.second.c_str();
    }
    
    // Save completed folders
    JsonArray folders = doc.createNestedArray("completed_datalog_folders");
    for (const String& folder : completedDatalogFolders) {
        folders.add(folder);
    }
    
    // Save pending folders
    JsonObject pendingFolders = doc.createNestedObject("pending_datalog_folders");
    for (const auto& pair : pendingDatalogFolders) {
        pendingFolders[pair.first.c_str()] = pair.second;
    }
    
    // Save retry tracking
    doc["current_retry_folder"] = currentRetryFolder.c_str();
    doc["current_retry_count"] = currentRetryCount;
    
    // Write to temporary file first to avoid corruption
    String tempFilePath = stateFilePath + ".tmp";
    File file = sd.open(tempFilePath, FILE_WRITE);
    if (!file) {
        LOGF("[UploadStateManager] ERROR: Failed to open state file for writing: %s", tempFilePath.c_str());
        return false;
    }
    
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    if (bytesWritten == 0) {
        LOG("[UploadStateManager] ERROR: Failed to write state file (0 bytes written)");
        sd.remove(tempFilePath);  // Clean up failed temp file
        return false;
    }
    
    // Verify the temp file was written correctly
    File verifyFile = sd.open(tempFilePath, FILE_READ);
    if (!verifyFile) {
        LOG("[UploadStateManager] ERROR: Failed to verify temp state file");
        sd.remove(tempFilePath);
        return false;
    }
    
    size_t verifySize = verifyFile.size();
    verifyFile.close();
    
    if (verifySize != bytesWritten) {
        LOG_DEBUGF("[UploadStateManager] ERROR: State file size mismatch (wrote %u bytes, file is %u bytes)", 
             bytesWritten, verifySize);
        sd.remove(tempFilePath);
        return false;
    }
    
    // Remove old state file if it exists
    if (sd.exists(stateFilePath)) {
        if (!sd.remove(stateFilePath)) {
            LOG_DEBUG("[UploadStateManager] WARNING: Failed to remove old state file");
            // Continue anyway - rename might still work
        }
    }
    
    // Rename temp file to actual state file
    if (!sd.rename(tempFilePath, stateFilePath)) {
        LOG("[UploadStateManager] ERROR: Failed to rename temp state file");
        sd.remove(tempFilePath);  // Clean up
        return false;
    }
    
    LOG_DEBUGF("[UploadStateManager] State file saved successfully (%u bytes)", bytesWritten);
    return true;
}
