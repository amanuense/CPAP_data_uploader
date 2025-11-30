#include "FileUploader.h"
#include "Logger.h"
#include <SD_MMC.h>

#ifdef ENABLE_TEST_WEBSERVER
#include "TestWebServer.h"
#endif

// Constructor
FileUploader::FileUploader(Config* cfg, WiFiManager* wifi) 
    : config(cfg),
      stateManager(nullptr),
      budgetManager(nullptr),
      scheduleManager(nullptr),
      wifiManager(wifi),
#ifdef ENABLE_TEST_WEBSERVER
      webServer(nullptr),
#endif
      lastSdReleaseTime(0)
#ifdef ENABLE_SMB_UPLOAD
      , smbUploader(nullptr)
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
      , webdavUploader(nullptr)
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
      , sleephqUploader(nullptr)
#endif
{
}

// Destructor
FileUploader::~FileUploader() {
    if (stateManager) delete stateManager;
    if (budgetManager) delete budgetManager;
    if (scheduleManager) delete scheduleManager;
#ifdef ENABLE_SMB_UPLOAD
    if (smbUploader) delete smbUploader;
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
    if (webdavUploader) delete webdavUploader;
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
    if (sleephqUploader) delete sleephqUploader;
#endif
}

// Initialize all components and load upload state
bool FileUploader::begin(fs::FS &sd) {
    LOG("[FileUploader] Initializing components...");
    
    // Initialize UploadStateManager
    stateManager = new UploadStateManager();
    if (!stateManager->begin(sd)) {
        LOG("[FileUploader] WARNING: Failed to load upload state, starting fresh");
        // Continue anyway - stateManager will work with empty state
    }
    
    // Initialize TimeBudgetManager
    budgetManager = new TimeBudgetManager();
    
    // Initialize ScheduleManager
    scheduleManager = new ScheduleManager();
    if (!scheduleManager->begin(
            config->getUploadHour(),
            config->getGmtOffsetHours())) {
        LOG("[FileUploader] ERROR: Failed to initialize ScheduleManager");
        return false;
    }
    
    // Restore last upload timestamp from state
    scheduleManager->setLastUploadTimestamp(stateManager->getLastUploadTimestamp());
    
    // Initialize appropriate uploader based on endpoint type and build flags
    String endpointType = config->getEndpointType();
    LOGF("[FileUploader] Endpoint type: %s", endpointType.c_str());
    
#ifdef ENABLE_SMB_UPLOAD
    if (endpointType == "SMB") {
        smbUploader = new SMBUploader(
            config->getEndpoint(),
            config->getEndpointUser(),
            config->getEndpointPassword()
        );
        
        // Note: We don't call begin() here because we may not have WiFi yet
        // Connection will be established when needed during upload
        LOG("[FileUploader] SMBUploader created (will connect during upload)");
    } else
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
    if (endpointType == "WEBDAV") {
        webdavUploader = new WebDAVUploader(
            config->getEndpoint(),
            config->getEndpointUser(),
            config->getEndpointPassword()
        );
        
        LOG("[FileUploader] WebDAVUploader created (will connect during upload)");
    } else
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
    if (endpointType == "SLEEPHQ") {
        sleephqUploader = new SleepHQUploader(
            config->getEndpoint(),
            config->getEndpointUser(),
            config->getEndpointPassword()
        );
        
        LOG("[FileUploader] SleepHQUploader created (will connect during upload)");
    } else
#endif
    {
        LOGF("[FileUploader] ERROR: Unsupported or disabled endpoint type: %s", endpointType.c_str());
        LOG("[FileUploader] Supported types (based on build flags):");
#ifdef ENABLE_SMB_UPLOAD
        LOG("[FileUploader]   - SMB (enabled)");
#else
        LOG("[FileUploader]   - SMB (disabled - compile with -DENABLE_SMB_UPLOAD)");
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
        LOG("[FileUploader]   - WEBDAV (enabled)");
#else
        LOG("[FileUploader]   - WEBDAV (disabled - compile with -DENABLE_WEBDAV_UPLOAD)");
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
        LOG("[FileUploader]   - SLEEPHQ (enabled)");
#else
        LOG("[FileUploader]   - SLEEPHQ (disabled - compile with -DENABLE_SLEEPHQ_UPLOAD)");
#endif
        return false;
    }
    
    LOG("[FileUploader] Initialization complete");
    return true;
}

// Check if it's time to upload
bool FileUploader::shouldUpload() {
    if (!scheduleManager) {
        return false;
    }
    return scheduleManager->isUploadTime();
}



// Check if it's time to periodically release SD card control
// Returns true if SD was released and retaken successfully
// Returns false if unable to retake control (should abort upload)
bool FileUploader::checkAndReleaseSD(SDCardManager* sdManager) {
    unsigned long now = millis();
    unsigned long intervalMs = config->getSdReleaseIntervalSeconds() * 1000;
    
    // Check if it's time to release
    if (now - lastSdReleaseTime < intervalMs) {
        return true;  // Not time yet, continue
    }
    
    LOG_DEBUG("[FileUploader] Periodic SD card release - giving CPAP priority access");
    
    // Pause active time tracking
    budgetManager->pauseActiveTime();
    
    // Release SD card
    sdManager->releaseControl();
    
    // Wait configured time, handling web requests during the wait
    unsigned long waitMs = config->getSdReleaseWaitMs();
    LOG_DEBUGF("[FileUploader] Waiting %lu ms before retaking control...", waitMs);
    
#ifdef ENABLE_TEST_WEBSERVER
    // Handle web requests during wait to keep interface responsive
    if (webServer) {
        unsigned long waitStart = millis();
        while (millis() - waitStart < waitMs) {
            webServer->handleClient();
            delay(10);  // Small delay to prevent tight loop
        }
    } else {
        delay(waitMs);
    }
#else
    delay(waitMs);
#endif
    
    // Retake control
    LOG_DEBUG("[FileUploader] Attempting to retake SD card control...");
    if (!sdManager->takeControl()) {
        LOG_ERROR("[FileUploader] Failed to retake SD card control");
        LOG_WARN("[FileUploader] CPAP machine may be actively using SD card");
        return false;  // Abort upload
    }
    
    // Resume active time tracking
    budgetManager->resumeActiveTime();
    
    // Reset release timer
    lastSdReleaseTime = millis();
    
    LOG_DEBUG("[FileUploader] SD card control reacquired, resuming upload");
    return true;
}

// Main upload orchestration
bool FileUploader::uploadNewFiles(SDCardManager* sdManager, bool forceUpload) {
    fs::FS &sd = sdManager->getFS();
    LOG("[FileUploader] Starting upload orchestration...");
    
    // Check WiFi connection first
    if (!wifiManager || !wifiManager->isConnected()) {
        LOG_ERROR("[FileUploader] WiFi not connected - cannot upload");
        LOG_ERROR("[FileUploader] Please ensure WiFi connection is established before upload");
        return false;
    }
    
    // Check if it's time to upload (unless forced or retrying incomplete folders)
    bool hasIncompleteFolders = (stateManager->getIncompleteFoldersCount() > 0);
    if (!forceUpload && !hasIncompleteFolders && !shouldUpload()) {
        unsigned long secondsUntilNext = scheduleManager->getSecondsUntilNextUpload();
        LOG_DEBUGF("[FileUploader] Not upload time yet. Next upload in %lu hours", secondsUntilNext / 3600);
        return false;
    }
    
    if (hasIncompleteFolders && !forceUpload) {
        LOG("[FileUploader] Resuming upload for incomplete folders (retry)");
    }
    
    if (forceUpload) {
        LOG("[FileUploader] FORCED UPLOAD - bypassing schedule check");
    }
    LOG("[FileUploader] Upload time - starting session");
    
    // Start upload session with time budget
    if (!startUploadSession(sd)) {
        LOG_ERROR("[FileUploader] Failed to start upload session");
        return false;
    }
    
    bool anyUploaded = false;
    
    // Phase 1: Process DATALOG folders (newest first)
    LOG("[FileUploader] Phase 1: Processing DATALOG folders");
    std::vector<String> datalogFolders = scanDatalogFolders(sd);
    
    // Update total folders count for progress tracking
    stateManager->setTotalFoldersCount(datalogFolders.size() + stateManager->getCompletedFoldersCount());
    
    for (const String& folderName : datalogFolders) {
        // Check if we still have budget
        if (!budgetManager->hasBudget()) {
            LOG("[FileUploader] Time budget exhausted during DATALOG processing");
            break;
        }
        
        // Check for periodic SD card release
        if (!checkAndReleaseSD(sdManager)) {
            LOG_ERROR("[FileUploader] Failed to retake SD card control, aborting upload");
            break;
        }
        
        // Upload the folder
        if (uploadDatalogFolder(sdManager, folderName)) {
            anyUploaded = true;
            LOGF("[FileUploader] Completed folder: %s", folderName.c_str());
        } else {
            LOGF("[FileUploader] Folder upload interrupted: %s", folderName.c_str());
            // Budget exhausted or error - stop processing
            break;
        }
        
#ifdef ENABLE_TEST_WEBSERVER
        // Handle web requests between folder uploads
        if (webServer) {
            webServer->handleClient();
        }
#endif
    }
    
    // Phase 2: Process root and SETTINGS files (if budget remains)
    if (budgetManager->hasBudget()) {
        LOG("[FileUploader] Phase 2: Processing root and SETTINGS files");
        std::vector<String> rootSettingsFiles = scanRootAndSettingsFiles(sd);
        
        for (const String& filePath : rootSettingsFiles) {
            // Check if we still have budget
            if (!budgetManager->hasBudget()) {
                LOG("[FileUploader] Time budget exhausted during root/SETTINGS processing");
                break;
            }
            
            // Check for periodic SD card release
            if (!checkAndReleaseSD(sdManager)) {
                LOG_ERROR("[FileUploader] Failed to retake SD card control, aborting upload");
                break;
            }
            
            // Upload the file
            if (uploadSingleFile(sdManager, filePath)) {
                anyUploaded = true;
            }
            
#ifdef ENABLE_TEST_WEBSERVER
            // Handle web requests between file uploads
            if (webServer) {
                webServer->handleClient();
            }
#endif
        }
    } else {
        LOG("[FileUploader] Skipping root/SETTINGS files - no budget remaining");
    }
    
    // End upload session and save state
    endUploadSession(sd);
    
    // Return true only if all folders are completed
    bool allComplete = (stateManager->getIncompleteFoldersCount() == 0);
    LOG_DEBUGF("[FileUploader] Upload session complete. All folders done: %s", allComplete ? "Yes" : "No");
    
    return allComplete;
}

// Scan SD card for pending folders without uploading
bool FileUploader::scanPendingFolders(SDCardManager* sdManager) {
    LOG("[FileUploader] Scanning SD card for pending folders...");
    
    fs::FS &sd = sdManager->getFS();
    
    // Scan DATALOG folders
    std::vector<String> datalogFolders = scanDatalogFolders(sd);
    
    // Update total folders count for progress tracking
    stateManager->setTotalFoldersCount(datalogFolders.size() + stateManager->getCompletedFoldersCount());
    
    LOG_DEBUGF("[FileUploader] Found %d incomplete folders", datalogFolders.size());
    LOG_DEBUGF("[FileUploader] Total folders: %d (completed: %d, pending: %d)", 
         stateManager->getCompletedFoldersCount() + datalogFolders.size(),
         stateManager->getCompletedFoldersCount(),
         datalogFolders.size());
    
    return true;
}

// Scan DATALOG folders and sort by date (newest first)
std::vector<String> FileUploader::scanDatalogFolders(fs::FS &sd) {
    std::vector<String> folders;
    
    File root = sd.open("/DATALOG");
    if (!root) {
        LOG_WARN("[FileUploader] DATALOG folder not found - no therapy data to upload");
        return folders;
    }
    
    if (!root.isDirectory()) {
        LOG_ERROR("[FileUploader] /DATALOG exists but is not a directory");
        root.close();
        return folders;
    }
    
    // Scan for folders
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            String folderName = String(file.name());
            
            // Extract just the folder name (remove path prefix if present)
            int lastSlash = folderName.lastIndexOf('/');
            if (lastSlash >= 0) {
                folderName = folderName.substring(lastSlash + 1);
            }
            
            // Check if folder is already completed
            if (!stateManager->isFolderCompleted(folderName)) {
                folders.push_back(folderName);
                LOG_DEBUGF("[FileUploader] Found incomplete DATALOG folder: %s", folderName.c_str());
            } else {
                LOG_DEBUGF("[FileUploader] Skipping completed folder: %s", folderName.c_str());
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    // Sort folders by date (newest first) - folders are in YYYYMMDD format
    std::sort(folders.begin(), folders.end(), [](const String& a, const String& b) {
        return a > b;  // Descending order (newest first)
    });
    
    LOG_DEBUGF("[FileUploader] Found %d incomplete DATALOG folders", folders.size());
    
    return folders;
}

// Scan files in a specific folder
std::vector<String> FileUploader::scanFolderFiles(fs::FS &sd, const String& folderPath) {
    std::vector<String> files;
    
    File folder = sd.open(folderPath);
    if (!folder) {
        LOG_ERRORF("[FileUploader] Failed to open folder: %s", folderPath.c_str());
        LOG_ERROR("[FileUploader] SD card may be experiencing read errors");
        return files;
    }
    
    if (!folder.isDirectory()) {
        LOG_ERRORF("[FileUploader] Path exists but is not a directory: %s", folderPath.c_str());
        folder.close();
        return files;
    }
    
    // Scan for .edf files
    File file = folder.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            
            // Extract just the file name (remove path prefix if present)
            int lastSlash = fileName.lastIndexOf('/');
            if (lastSlash >= 0) {
                fileName = fileName.substring(lastSlash + 1);
            }
            
            // Check if it's an .edf file
            if (fileName.endsWith(".edf") || fileName.endsWith(".EDF")) {
                files.push_back(fileName);
            }
        }
        file.close();
        file = folder.openNextFile();
    }
    folder.close();
    
    LOG_DEBUGF("[FileUploader] Found %d .edf files in %s", files.size(), folderPath.c_str());
    
    return files;
}

// Scan root and SETTINGS files that need tracking
std::vector<String> FileUploader::scanRootAndSettingsFiles(fs::FS &sd) {
    std::vector<String> files;
    
    // Root files to track
    const char* rootFiles[] = {
        "/identification.json",
        "/identification.crc",
        "/STR.edf",
        "/journal.jnl"
    };
    
    for (int i = 0; i < 3; i++) {
        if (sd.exists(rootFiles[i])) {
            // Check if file has changed
            if (stateManager->hasFileChanged(sd, rootFiles[i])) {
                files.push_back(String(rootFiles[i]));
                LOG_DEBUGF("[FileUploader] Root file changed: %s", rootFiles[i]);
            }
        }
    }
    
    // SETTINGS files to track
    const char* settingsFiles[] = {
        "/SETTINGS/CurrentSettings.json",
        "/SETTINGS/CurrentSettings.crc"
    };
    
    for (int i = 0; i < 2; i++) {
        if (sd.exists(settingsFiles[i])) {
            // Check if file has changed
            if (stateManager->hasFileChanged(sd, settingsFiles[i])) {
                files.push_back(String(settingsFiles[i]));
                LOG_DEBUGF("[FileUploader] SETTINGS file changed: %s", settingsFiles[i]);
            }
        }
    }
    
    LOG_DEBUGF("[FileUploader] Found %d changed root/SETTINGS files", files.size());
    
    return files;
}

// Start upload session with time budget
bool FileUploader::startUploadSession(fs::FS &sd) {
    LOG("[FileUploader] Starting upload session");
    
    // Get session duration from config
    unsigned long sessionDuration = config->getSessionDurationSeconds();
    
    // Check if we need to apply retry multiplier
    int retryCount = stateManager->getCurrentRetryCount();
    
    // Apply multiplier for any retry attempt to increase budget
    if (retryCount > 0) {
        int multiplier = retryCount + 1;  // +1 so first retry gets 2x budget
        LOG_DEBUGF("[FileUploader] Applying retry multiplier: %dx (retry count: %d)", multiplier, retryCount);
        budgetManager->startSession(sessionDuration, multiplier);
    } else {
        budgetManager->startSession(sessionDuration);
    }
    
    LOG_DEBUGF("[FileUploader] Session budget: %lu ms (active time only)", budgetManager->getRemainingBudgetMs());
    LOG_DEBUGF("[FileUploader] Periodic SD release: every %d seconds", config->getSdReleaseIntervalSeconds());
    
    // Initialize periodic release timer
    lastSdReleaseTime = millis();
    
    return true;
}

// End upload session and save state
void FileUploader::endUploadSession(fs::FS &sd) {
    LOG("[FileUploader] Ending upload session");
    
    // Save upload state
    if (!stateManager->save(sd)) {
        LOG_ERROR("[FileUploader] Failed to save upload state");
        LOG_WARN("[FileUploader] Upload progress may be lost - will retry from last saved state");
    }
    
    // Only mark upload as completed if there are no incomplete folders
    bool hasIncompleteFolders = (stateManager->getIncompleteFoldersCount() > 0);
    if (!hasIncompleteFolders) {
        // Update last upload timestamp
        time_t now;
        time(&now);
        stateManager->setLastUploadTimestamp((unsigned long)now);
        scheduleManager->markUploadCompleted();
        LOG("[FileUploader] All folders completed - upload session marked as done");
    } else {
        LOG("[FileUploader] Incomplete folders remain - upload will retry");
    }
    
    // Calculate wait time
    unsigned long waitTimeMs = budgetManager->getWaitTimeMs();
    LOG_DEBUGF("[FileUploader] Wait time before next session: %lu seconds", waitTimeMs / 1000);
    
    // Save state again with updated timestamp
    if (!stateManager->save(sd)) {
        LOG_ERROR("[FileUploader] Failed to save final state with timestamp");
        LOG_WARN("[FileUploader] Next upload may occur sooner than scheduled");
    }
}

// Upload all files in a DATALOG folder
bool FileUploader::uploadDatalogFolder(SDCardManager* sdManager, const String& folderName) {
    fs::FS &sd = sdManager->getFS();
    
    // Get retry count BEFORE setting current folder (in case it's a different folder)
    String currentRetryFolder = stateManager->getCurrentRetryFolder();
    int retryCount = (currentRetryFolder == folderName) ? stateManager->getCurrentRetryCount() : 0;
    
    // Set this as the current retry folder
    stateManager->setCurrentRetryFolder(folderName);
    
    // Log appropriately based on retry status
    if (retryCount > 0) {
        LOGF("[FileUploader] RETRY ATTEMPT %d: Uploading DATALOG folder: %s", retryCount + 1, folderName.c_str());
    } else {
        LOGF("[FileUploader] Uploading DATALOG folder: %s", folderName.c_str());
    }
    
    // Build folder path
    String folderPath = "/DATALOG/" + folderName;
    
    // Scan for files in the folder
    std::vector<String> files = scanFolderFiles(sd, folderPath);
    
    if (files.empty()) {
        LOG_WARN("[FileUploader] No .edf files found in folder");
        // Mark as completed even if empty
        stateManager->markFolderCompleted(folderName);
        stateManager->clearCurrentRetry();
        return true;
    }
    
    // Upload each file
    int uploadedCount = 0;
    for (const String& fileName : files) {
        // Check for periodic SD card release before each file
        if (!checkAndReleaseSD(sdManager)) {
            LOG_ERROR("[FileUploader] Failed to retake SD card control during folder upload");
            stateManager->incrementCurrentRetryCount();
            stateManager->save(sd);
            return false;
        }
        
        // Check time budget before uploading
        String localPath = folderPath + "/" + fileName;
        
        // Get file size
        File file = sd.open(localPath);
        if (!file) {
            LOG_ERRORF("[FileUploader] Cannot open file for reading: %s", localPath.c_str());
            LOG_ERROR("[FileUploader] File may be corrupted or SD card has read errors");
            LOG_WARN("[FileUploader] Skipping this file and continuing with next file");
            continue;  // Skip this file but continue with others
        }
        
        unsigned long fileSize = file.size();
        
        // Sanity check file size
        if (fileSize == 0) {
            LOG_WARNF("[FileUploader] File is empty: %s", localPath.c_str());
            file.close();
            continue;  // Skip empty files
        }
        
        file.close();
        
        // Check if we have budget for this file
        if (!budgetManager->canUploadFile(fileSize)) {
            LOG("[FileUploader] Insufficient time budget for remaining files");
            LOGF("[FileUploader] Successfully uploaded %d of %d files before budget exhaustion", uploadedCount, files.size());
            LOG("[FileUploader] This is normal - upload will resume in next session");
            
            // Increment retry count for this folder (partial upload)
            stateManager->incrementCurrentRetryCount();
            if (!stateManager->save(sd)) {
                LOG("[FileUploader] WARNING: Failed to save state after partial upload");
            }
            return false;  // Session interrupted due to budget
        }
        
        // Upload the file
        String remotePath = folderPath + "/" + fileName;
        unsigned long bytesTransferred = 0;
        unsigned long uploadStartTime = millis();
        
        // Log file upload with retry information if applicable
        int currentRetryCount = stateManager->getCurrentRetryCount();
        if (currentRetryCount > 0) {
            LOGF("[FileUploader] RETRY ATTEMPT %d: Uploading file: %s (%lu bytes)", 
                 currentRetryCount + 1, fileName.c_str(), fileSize);
        } else {
            LOGF("[FileUploader] Uploading file: %s (%lu bytes)", fileName.c_str(), fileSize);
        }
        
        bool uploadSuccess = false;
        
        // Use the appropriate uploader based on configuration
#ifdef ENABLE_SMB_UPLOAD
        if (smbUploader && config->getEndpointType() == "SMB") {
            // Ensure SMB connection is established
            if (!smbUploader->isConnected()) {
                LOG_DEBUG("[FileUploader] SMB not connected, attempting to connect...");
                if (!smbUploader->begin()) {
                    LOG_ERROR("[FileUploader] Failed to connect to SMB share");
                    LOG_ERROR("[FileUploader] Check network connectivity and SMB credentials");
                    stateManager->incrementCurrentRetryCount();
                    stateManager->save(sd);
                    return false;
                }
            }
            
            uploadSuccess = smbUploader->upload(localPath, remotePath, sd, bytesTransferred);
        } else
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
        if (webdavUploader && config->getEndpointType() == "WEBDAV") {
            // Ensure WebDAV connection is established
            if (!webdavUploader->isConnected()) {
                LOG_DEBUG("[FileUploader] WebDAV not connected, attempting to connect...");
                if (!webdavUploader->begin()) {
                    LOG_ERROR("[FileUploader] Failed to connect to WebDAV server");
                    LOG_ERROR("[FileUploader] Check network connectivity and WebDAV credentials");
                    stateManager->incrementCurrentRetryCount();
                    stateManager->save(sd);
                    return false;
                }
            }
            
            uploadSuccess = webdavUploader->upload(localPath, remotePath, sd, bytesTransferred);
        } else
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
        if (sleephqUploader && config->getEndpointType() == "SLEEPHQ") {
            // Ensure SleepHQ connection is established
            if (!sleephqUploader->isConnected()) {
                LOG_DEBUG("[FileUploader] SleepHQ not connected, attempting to connect...");
                if (!sleephqUploader->begin()) {
                    LOG_ERROR("[FileUploader] Failed to connect to SleepHQ service");
                    LOG_ERROR("[FileUploader] Check network connectivity and API credentials");
                    stateManager->incrementCurrentRetryCount();
                    stateManager->save(sd);
                    return false;
                }
            }
            
            uploadSuccess = sleephqUploader->upload(localPath, remotePath, sd, bytesTransferred);
        } else
#endif
        {
            LOG_ERROR("[FileUploader] No uploader available for configured endpoint type");
            LOG_ERROR("[FileUploader] Check ENDPOINT_TYPE in config.json and build flags");
            stateManager->incrementCurrentRetryCount();
            stateManager->save(sd);
            return false;
        }
        
        if (!uploadSuccess) {
            LOG_ERRORF("[FileUploader] Failed to upload file: %s", localPath.c_str());
            LOG_ERROR("[FileUploader] This may be due to:");
            LOG_ERROR("[FileUploader]   - Network connectivity issues");
            LOG_ERROR("[FileUploader]   - SMB server unavailable or overloaded");
            LOG_ERROR("[FileUploader]   - Insufficient permissions on remote share");
            LOG_ERROR("[FileUploader]   - Disk space issues on remote server");
            LOG_WARNF("[FileUploader] Successfully uploaded %d files before failure", uploadedCount);
            
            // Don't mark folder as completed, will retry
            stateManager->incrementCurrentRetryCount();
            if (!stateManager->save(sd)) {
                LOG_WARN("[FileUploader] Failed to save state after upload error");
            }
            return false;  // Stop processing this folder
        }
        
        // Record upload for transmission rate calculation (skip small files < 5KB)
        unsigned long uploadTime = millis() - uploadStartTime;
        if (bytesTransferred >= 5120) {  // 5KB minimum for rate calculation
            budgetManager->recordUpload(bytesTransferred, uploadTime);
        }
        
        uploadedCount++;
        LOGF("[FileUploader] Uploaded: %s (%lu bytes)", fileName.c_str(), bytesTransferred);
        LOG_DEBUGF("[FileUploader] Budget remaining: %lu ms", budgetManager->getRemainingBudgetMs());
    }
    
    // All files uploaded successfully
    LOGF("[FileUploader] Successfully uploaded all %d files in folder", uploadedCount);
    
    // Mark folder as completed
    stateManager->markFolderCompleted(folderName);
    
    // Reset retry count for this folder
    stateManager->clearCurrentRetry();
    
    // Save state
    stateManager->save(sd);
    
    return true;
}

// Upload a single file (for root and SETTINGS files)
bool FileUploader::uploadSingleFile(SDCardManager* sdManager, const String& filePath) {
    fs::FS &sd = sdManager->getFS();
    LOGF("[FileUploader] Uploading single file: %s", filePath.c_str());
    
    // Check if file exists
    if (!sd.exists(filePath)) {
        LOG_ERRORF("[FileUploader] File does not exist: %s", filePath.c_str());
        LOG_WARN("[FileUploader] File may have been deleted or SD card structure changed");
        return false;
    }
    
    // Get file size
    File file = sd.open(filePath);
    if (!file) {
        LOG_ERRORF("[FileUploader] Cannot open file for reading: %s", filePath.c_str());
        LOG_ERROR("[FileUploader] File may be corrupted or SD card has read errors");
        return false;
    }
    
    unsigned long fileSize = file.size();
    
    // Sanity check file size
    if (fileSize == 0) {
        LOG_WARNF("[FileUploader] File is empty: %s", filePath.c_str());
        file.close();
        return true;  // Consider empty file as "uploaded" (skip it)
    }
    
    file.close();
    
    // Check if we have budget for this file
    if (!budgetManager->canUploadFile(fileSize)) {
        LOGF("[FileUploader] Insufficient time budget for file: %s", filePath.c_str());
        LOG("[FileUploader] File will be uploaded in next session");
        return false;  // Not an error, just out of budget
    }
    
    // Check if file has changed (checksum comparison)
    if (!stateManager->hasFileChanged(sd, filePath)) {
        LOG_DEBUG("[FileUploader] File unchanged, skipping upload");
        return true;  // Not an error, just no need to upload
    }
    
    // Upload the file
    unsigned long bytesTransferred = 0;
    unsigned long uploadStartTime = millis();
    
    bool uploadSuccess = false;
    
    // Use the appropriate uploader based on configuration
#ifdef ENABLE_SMB_UPLOAD
    if (smbUploader && config->getEndpointType() == "SMB") {
        // Ensure SMB connection is established
        if (!smbUploader->isConnected()) {
            LOG_DEBUG("[FileUploader] SMB not connected, attempting to connect...");
            if (!smbUploader->begin()) {
                LOG_ERROR("[FileUploader] Failed to connect to SMB share");
                LOG_ERROR("[FileUploader] Check network connectivity and SMB credentials");
                return false;
            }
        }
        
        uploadSuccess = smbUploader->upload(filePath, filePath, sd, bytesTransferred);
    } else
#endif
#ifdef ENABLE_WEBDAV_UPLOAD
    if (webdavUploader && config->getEndpointType() == "WEBDAV") {
        // Ensure WebDAV connection is established
        if (!webdavUploader->isConnected()) {
            LOG_DEBUG("[FileUploader] WebDAV not connected, attempting to connect...");
            if (!webdavUploader->begin()) {
                LOG_ERROR("[FileUploader] Failed to connect to WebDAV server");
                LOG_ERROR("[FileUploader] Check network connectivity and WebDAV credentials");
                return false;
            }
        }
        
        uploadSuccess = webdavUploader->upload(filePath, filePath, sd, bytesTransferred);
    } else
#endif
#ifdef ENABLE_SLEEPHQ_UPLOAD
    if (sleephqUploader && config->getEndpointType() == "SLEEPHQ") {
        // Ensure SleepHQ connection is established
        if (!sleephqUploader->isConnected()) {
            LOG_DEBUG("[FileUploader] SleepHQ not connected, attempting to connect...");
            if (!sleephqUploader->begin()) {
                LOG_ERROR("[FileUploader] Failed to connect to SleepHQ service");
                LOG_ERROR("[FileUploader] Check network connectivity and API credentials");
                return false;
            }
        }
        
        uploadSuccess = sleephqUploader->upload(filePath, filePath, sd, bytesTransferred);
    } else
#endif
    {
        LOG_ERROR("[FileUploader] No uploader available for configured endpoint type");
        LOG_ERROR("[FileUploader] Check ENDPOINT_TYPE in config.json and build flags");
        return false;
    }
    
    if (!uploadSuccess) {
        LOG_ERROR("[FileUploader] Failed to upload file");
        LOG_ERROR("[FileUploader] This may be due to network issues or SMB server problems");
        return false;
    }
    
    // Record upload for transmission rate calculation (skip small files < 5KB)
    unsigned long uploadTime = millis() - uploadStartTime;
    if (bytesTransferred >= 5120) {  // 5KB minimum for rate calculation
        budgetManager->recordUpload(bytesTransferred, uploadTime);
    }
    
    // Calculate and store new checksum
    // The hasFileChanged method already calculated it, but we need to mark it as uploaded
    // We'll recalculate to ensure consistency
    File checksumFile = sd.open(filePath);
    if (checksumFile) {
        // Calculate checksum (simplified - actual implementation in UploadStateManager)
        String checksum = "";  // Will be calculated by markFileUploaded
        checksumFile.close();
        
        // Mark file as uploaded with its checksum
        stateManager->markFileUploaded(filePath, checksum);
        
        // Save state
        stateManager->save(sd);
    }
    
    LOGF("[FileUploader] Successfully uploaded: %s (%lu bytes)", filePath.c_str(), bytesTransferred);
    
    return true;
}
