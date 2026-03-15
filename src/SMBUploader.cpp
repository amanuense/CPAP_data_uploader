#include "SMBUploader.h"
#include "UploadFSM.h"
#include "Logger.h"
#include "NetworkRecovery.h"

#ifdef ENABLE_SMB_UPLOAD

#include <fcntl.h>  // For O_WRONLY, O_CREAT, O_TRUNC flags
#include <errno.h>
#include <string.h>
#include <WiFi.h>

// Include libsmb2 headers
// Note: These will be available when libsmb2 is added as ESP-IDF component
extern "C" {
    #include "smb2/smb2.h"
    #include "smb2/libsmb2.h"
}

// Buffer size for file streaming (8KB to avoid fragmentation in mixed-backend mode)
#define UPLOAD_BUFFER_SIZE 8192
#define UPLOAD_BUFFER_FALLBACK_SIZE 4096
#define SMB_COMMAND_TIMEOUT_SECONDS 15
#define SMB_UPLOAD_MAX_ATTEMPTS 2
#define SMB_WRITE_EAGAIN_RETRIES 6
#define SMB_WRITE_EAGAIN_BASE_DELAY_MS 20

extern volatile unsigned long g_uploadHeartbeat;

static inline void feedUploadHeartbeat() {
    g_uploadHeartbeat = millis();
}

static bool isRecoverableSmbWriteError(int errorCode, const char* smbError) {
    if (errorCode == ETIMEDOUT ||
        errorCode == ENETRESET ||
        errorCode == ECONNRESET ||
        errorCode == ENOTCONN ||
        errorCode == EPIPE ||
        errorCode == EAGAIN
#ifdef EWOULDBLOCK
        || errorCode == EWOULDBLOCK
#endif
    ) {
        return true;
    }

    if (!smbError) {
        return false;
    }

    return strstr(smbError, "STATUS_IO_TIMEOUT") != nullptr ||
           strstr(smbError, "STATUS_CONNECTION_DISCONNECTED") != nullptr ||
           strstr(smbError, "STATUS_NETWORK_NAME_DELETED") != nullptr ||
           strstr(smbError, "Wrong signature in received PDU") != nullptr ||
           strstr(smbError, "Wrong signature") != nullptr ||
           strstr(smbError, "smb2_service: POLLERR") != nullptr ||
           strstr(smbError, "POLLERR") != nullptr ||
           strstr(smbError, "Unknown socket error") != nullptr ||
           strstr(smbError, "Connect failed with errno") != nullptr ||
           strstr(smbError, "Host is unreachable") != nullptr ||
           strstr(smbError, "timed out") != nullptr ||
           strstr(smbError, "TIMEOUT") != nullptr ||
           strstr(smbError, "Connection reset") != nullptr;
}

static bool isSmbPduAllocationError(const char* smbError) {
    return smbError &&
           (strstr(smbError, "Failed to allocate pdu") != nullptr ||
            strstr(smbError, "allocate pdu") != nullptr);
}

static bool isTransientSmbSocketBackpressure(int errorCode, const char* smbError) {
    if (errorCode == EAGAIN
#ifdef EWOULDBLOCK
        || errorCode == EWOULDBLOCK
#endif
    ) {
        return true;
    }

    if (!smbError) {
        return false;
    }

    return strstr(smbError, "Resource temporarily unavailable") != nullptr ||
           strstr(smbError, "No more processes") != nullptr ||
           strstr(smbError, "EAGAIN") != nullptr;
}

static bool recoverWiFiAfterSmbTransportFailure() {
    // Delegate to the coordinated cycle which enforces the SMB-active guard,
    // the 45-second cooldown, and the in-progress-cycle wait — preventing the
    // double-cycle ASSOC_LEAVE storms that caused EHOSTUNREACH (errno 113).
    // g_smbConnectionActive is already false here because disconnect() was
    // called before this function, so the SMB guard does not block us.
    return tryCoordinatedWifiCycle(true);
}

SMBUploader::SMBUploader(const String& endpoint, const String& user, const String& password)
    : smbUser(user), smbPassword(password), smb2(nullptr), connected(false),
      uploadBuffer(nullptr), uploadBufferSize(0), lastVerifiedParentDir("") {
    parseEndpoint(endpoint);
}

SMBUploader::~SMBUploader() {
    end();
    if (uploadBuffer) {
        free(uploadBuffer);
        uploadBuffer = nullptr;
        uploadBufferSize = 0;
    }
}

bool SMBUploader::parseEndpoint(const String& endpoint) {
    // Expected format: //server/share or //server/share/path
    // We only need server and share for connection
    
    if (!endpoint.startsWith("//")) {
        LOG("[SMB] ERROR: Invalid endpoint format, must start with //");
        LOGF("[SMB] Got: %s", endpoint.c_str());
        LOG("[SMB] Expected format: //server/share");
        return false;
    }
    
    // Remove leading //
    String path = endpoint.substring(2);
    
    // Find first slash to separate server from share
    int firstSlash = path.indexOf('/');
    if (firstSlash == -1) {
        LOG("[SMB] ERROR: Invalid endpoint format, missing share name");
        LOGF("[SMB] Got: %s", endpoint.c_str());
        LOG("[SMB] Expected format: //server/share");
        return false;
    }
    
    smbServer = path.substring(0, firstSlash);
    
    // Find second slash to separate share from path (if exists)
    int secondSlash = path.indexOf('/', firstSlash + 1);
    if (secondSlash == -1) {
        // No path component, just share
        smbShare = path.substring(firstSlash + 1);
        smbBasePath = "";
    } else {
        // Has path component, extract share name and base path
        smbShare = path.substring(firstSlash + 1, secondSlash);
        smbBasePath = path.substring(secondSlash + 1);
        // Remove trailing slash if present
        if (smbBasePath.endsWith("/")) {
            smbBasePath = smbBasePath.substring(0, smbBasePath.length() - 1);
        }
    }
    
    if (smbServer.isEmpty() || smbShare.isEmpty()) {
        LOG("[SMB] ERROR: Invalid endpoint, server or share is empty after parsing");
        LOGF("[SMB] Server: '%s', Share: '%s'", smbServer.c_str(), smbShare.c_str());
        return false;
    }
    
    if (smbBasePath.isEmpty()) {
        LOG_DEBUGF("[SMB] Parsed endpoint - Server: %s, Share: %s", smbServer.c_str(), smbShare.c_str());
    } else {
        LOG_DEBUGF("[SMB] Parsed endpoint - Server: %s, Share: %s, BasePath: %s", 
             smbServer.c_str(), smbShare.c_str(), smbBasePath.c_str());
    }
    
    return true;
}

bool SMBUploader::connect() {
    if (connected) {
        return true;
    }
    
    if (smbServer.isEmpty() || smbShare.isEmpty()) {
        LOG("[SMB] ERROR: Cannot connect, endpoint not parsed correctly");
        LOG("[SMB] Check ENDPOINT configuration in config.txt");
        return false;
    }
    
    // Create SMB2 context
    smb2 = smb2_init_context();
    if (smb2 == nullptr) {
        LOG("[SMB] ERROR: Failed to initialize SMB context");
        LOG("[SMB] This may indicate a memory allocation failure");
        return false;
    }

    // Critical: libsmb2 synchronous calls block indefinitely unless timeout is configured.
    smb2_set_timeout(smb2, SMB_COMMAND_TIMEOUT_SECONDS);
    LOG_DEBUGF("[SMB] libsmb2 command timeout set to %d seconds", SMB_COMMAND_TIMEOUT_SECONDS);
    
    // Set security mode (allow guest if no credentials)
    if (smbUser.isEmpty()) {
        LOG("[SMB] WARNING: No credentials provided, attempting guest access");
        smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
    } else {
        smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
        smb2_set_user(smb2, smbUser.c_str());
        smb2_set_password(smb2, smbPassword.c_str());
    }
    
    // Connect to server
    LOGF("[SMB] Connecting to //%s/%s", smbServer.c_str(), smbShare.c_str());
    
    if (smb2_connect_share(smb2, smbServer.c_str(), smbShare.c_str(), nullptr) < 0) {
        const char* error = smb2_get_error(smb2);
        LOGF("[SMB] ERROR: Connection failed: %s", error);
        LOG("[SMB] Possible causes:");
        LOG("[SMB]   - Server unreachable (check network/firewall)");
        LOG("[SMB]   - Invalid credentials (check ENDPOINT_USER/ENDPOINT_PASS)");
        LOG("[SMB]   - Share does not exist or is not accessible");
        LOG("[SMB]   - SMB protocol version mismatch");
        smb2_destroy_context(smb2);
        smb2 = nullptr;
        return false;
    }
    
    connected = true;
    g_smbConnectionActive = true;
    lastVerifiedParentDir = "";
    LOG("[SMB] Connected successfully");
    
    // Test if we can access the base path (if configured)
    if (!smbBasePath.isEmpty()) {
        String testPath = "/" + smbBasePath;
        struct smb2_stat_64 st;
        int stat_result = smb2_stat(smb2, testPath.c_str(), &st);
        if (stat_result == 0) {
            if (st.smb2_type == SMB2_TYPE_DIRECTORY) {
                LOG_DEBUGF("[SMB] Base path verified: %s (exists and is accessible)", testPath.c_str());
            } else {
                LOGF("[SMB] WARNING: Base path exists but is not a directory: %s", testPath.c_str());
            }
        } else {
            const char* error = smb2_get_error(smb2);
            LOG_DEBUGF("[SMB] WARNING: Cannot access base path %s: %s", testPath.c_str(), error);
            LOG_DEBUG("[SMB] Will attempt to create it during upload");
        }
    }
    
    return true;
}

void SMBUploader::disconnect() {
    g_smbConnectionActive = false;
    if (smb2 != nullptr) {
        if (connected) {
            smb2_disconnect_share(smb2);
            connected = false;
        }
        smb2_destroy_context(smb2);
        smb2 = nullptr;
    }

    lastVerifiedParentDir = "";
}

bool SMBUploader::begin() {
    return connect();
}

void SMBUploader::end() {
    disconnect();
}

bool SMBUploader::isConnected() const {
    return connected;
}

bool SMBUploader::allocateBuffer(size_t size) {
    // Free existing buffer if already allocated
    if (uploadBuffer) {
        free(uploadBuffer);
        uploadBuffer = nullptr;
        uploadBufferSize = 0;
    }
    
    // Allocate new buffer
    uploadBuffer = (uint8_t*)malloc(size);
    if (!uploadBuffer) {
        LOG_ERRORF("[SMB] Failed to allocate upload buffer (%u bytes)", size);
        LOG("[SMB] System may be low on memory");
        return false;
    }
    
    uploadBufferSize = size;
    LOGF("[SMB] Allocated upload buffer: %u bytes", uploadBufferSize);
    return true;
}

bool SMBUploader::createDirectory(const String& path) {
    if (!connected) {
        LOG("[SMB] ERROR: Not connected - cannot create directory");
        return false;
    }
    
    if (path.isEmpty() || path == "/") {
        return true;  // Root always exists
    }
    
    // Remove leading slash for libsmb2 compatibility (paths are relative to share)
    String cleanPath = path;
    if (cleanPath.startsWith("/")) {
        cleanPath = cleanPath.substring(1);
    }
    
    if (cleanPath.isEmpty()) {
        return true;  // Root always exists
    }
    
    // Check heap before attempting directory operations.
    // IMPORTANT: Do not skip create/check in low-memory mode, otherwise we can
    // incorrectly report success and then fail smb2_open with PATH_NOT_FOUND.
    uint32_t maxAlloc = ESP.getMaxAllocHeap();
    if (maxAlloc < 50000) {
        LOG_WARNF("[SMB] Low memory (%u bytes), still validating/creating directory: %s",
                  maxAlloc,
                  cleanPath.c_str());
    }
    
    // Check if directory already exists
    struct smb2_stat_64 st;
    int stat_result = smb2_stat(smb2, cleanPath.c_str(), &st);
    if (stat_result == 0) {
        // Path exists, check if it's a directory
        if (st.smb2_type == SMB2_TYPE_DIRECTORY) {
            LOG_DEBUGF("[SMB] Directory already exists: %s", cleanPath.c_str());
            return true;  // Directory already exists
        } else {
            LOGF("[SMB] ERROR: Path exists but is not a directory: %s", cleanPath.c_str());
            LOG("[SMB] Cannot create directory - file with same name exists");
            return false;
        }
    } else {
        const char* statError = smb2_get_error(smb2);

        // Under heavy fragmentation/libsmb2 pressure, stat itself may fail due
        // to PDU allocation. Do not misclassify as "directory missing".
        if (isSmbPduAllocationError(statError)) {
            LOG_WARNF("[SMB] Directory stat failed due libsmb2 memory pressure for %s: %s",
                      cleanPath.c_str(),
                      statError);
            return false;
        }

        // Stat failed - directory might not exist or we might not have permissions
        LOGF("[SMB] Directory does not exist: %s (will create)", cleanPath.c_str());
    }
    
    // Directory doesn't exist, need to create it
    // First ensure parent directory exists
    int lastSlash = cleanPath.lastIndexOf('/');
    if (lastSlash > 0) {
        String parentPath = cleanPath.substring(0, lastSlash);
        if (!createDirectory(parentPath)) {
            LOGF("[SMB] ERROR: Failed to create parent directory: %s", parentPath.c_str());
            return false;  // Failed to create parent
        }
    }
    
    // Create this directory
    LOGF("[SMB] Creating directory: %s", cleanPath.c_str());
    
    int mkdir_result = smb2_mkdir(smb2, cleanPath.c_str());
    if (mkdir_result < 0) {
        const char* error = smb2_get_error(smb2);

        if (isSmbPduAllocationError(error)) {
            LOG_WARNF("[SMB] Directory create failed due libsmb2 memory pressure for %s: %s",
                      cleanPath.c_str(),
                      error);
            return false;
        }
        
        // Check if error is because directory already exists
        // STATUS_INVALID_PARAMETER can mean the directory already exists in some SMB implementations
        if (smb2_stat(smb2, cleanPath.c_str(), &st) == 0 && st.smb2_type == SMB2_TYPE_DIRECTORY) {
            LOG_DEBUGF("[SMB] Directory already exists (mkdir failed but stat succeeded): %s", cleanPath.c_str());
            return true;  // Directory exists, treat as success
        }
        
        // If we get STATUS_INVALID_PARAMETER, assume directory exists and continue
        // This is a workaround for SMB servers that return this error for existing directories
        if (strstr(error, "STATUS_INVALID_PARAMETER") != NULL) {
            LOG_DEBUGF("[SMB] WARNING: mkdir failed with STATUS_INVALID_PARAMETER for %s", cleanPath.c_str());
            LOG_DEBUG("[SMB] Assuming directory already exists, continuing...");
            return true;  // Assume directory exists
        }
        
        LOGF("[SMB] ERROR: Failed to create directory: %s", error);
        LOG("[SMB] Possible causes:");
        LOG("[SMB]   - Insufficient permissions");
        LOG("[SMB]   - Invalid directory name");
        LOG("[SMB]   - Network connection lost");
        return false;
    }
    
    LOGF("[SMB] Directory created successfully: %s", cleanPath.c_str());
    return true;
}

bool SMBUploader::upload(const String& localPath, const String& remotePath, 
                         fs::FS &sd, unsigned long& bytesTransferred) {
    bytesTransferred = 0;
    
    if (!connected) {
        LOG("SMB: Not connected");
        return false;
    }
    
    // Prepend base path if configured
    // Note: libsmb2 expects paths relative to share root WITHOUT leading slash
    String fullRemotePath = remotePath;
    if (!smbBasePath.isEmpty()) {
        // Remove leading slash from remotePath if present
        String cleanRemotePath = remotePath;
        if (cleanRemotePath.startsWith("/")) {
            cleanRemotePath = cleanRemotePath.substring(1);
        }
        fullRemotePath = smbBasePath + "/" + cleanRemotePath;
    } else if (fullRemotePath.startsWith("/")) {
        // Remove leading slash for libsmb2 compatibility
        fullRemotePath = fullRemotePath.substring(1);
    }
    
    for (int attempt = 1; attempt <= SMB_UPLOAD_MAX_ATTEMPTS; ++attempt) {
        bool shouldRetry = false;
        unsigned long attemptBytesTransferred = 0;

        if (attempt > 1) {
            LOG_WARNF("[SMB] Retry attempt %d/%d for %s",
                      attempt, SMB_UPLOAD_MAX_ATTEMPTS, localPath.c_str());
        }

        // Open local file from SD card
        File localFile = sd.open(localPath, FILE_READ);
        if (!localFile) {
            LOGF("[SMB] ERROR: Failed to open local file: %s", localPath.c_str());
            LOG("[SMB] File may not exist or SD card has read errors");
            return false;
        }

        size_t fileSize = localFile.size();

        // Sanity check file size
        if (fileSize == 0) {
            LOGF("[SMB] WARNING: File is empty: %s", localPath.c_str());
            localFile.close();
            return false;
        }

        LOG_DEBUGF("[SMB] Uploading %s (%u bytes)", localPath.c_str(), (unsigned int)fileSize);
        LOG_DEBUGF("[SMB] Remote path: %s", fullRemotePath.c_str());

        // Ensure parent directory exists
        String parentDir;
        int lastSlash = fullRemotePath.lastIndexOf('/');
        if (lastSlash > 0) {
            parentDir = fullRemotePath.substring(0, lastSlash);
            if (parentDir != lastVerifiedParentDir) {
                if (!createDirectory(parentDir)) {
                    uint32_t maxAllocNow = ESP.getMaxAllocHeap();
                    if (maxAllocNow < 50000) {
                        LOG_WARNF("[SMB] Parent directory check/create deferred under low memory (%u bytes): %s",
                                  maxAllocNow,
                                  parentDir.c_str());
                        LOG_WARN("[SMB] Proceeding with direct open; PATH_NOT_FOUND recovery will retry creation");
                    } else {
                        LOGF("[SMB] ERROR: Failed to create parent directory: %s", parentDir.c_str());
                        LOG("[SMB] Check permissions on remote share");
                        localFile.close();
                        return false;
                    }
                } else {
                    lastVerifiedParentDir = parentDir;
                    feedUploadHeartbeat();
                }
            }
        }

        // Open remote file for writing
        struct smb2fh* remoteFile = smb2_open(smb2, fullRemotePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (remoteFile == nullptr) {
            const char* error = smb2_get_error(smb2);

            // Defensive recovery: if parent path is missing, create it again and retry open once.
            if (!parentDir.isEmpty() && error &&
                (strstr(error, "STATUS_OBJECT_PATH_NOT_FOUND") != nullptr ||
                 strstr(error, "PATH_NOT_FOUND") != nullptr)) {
                LOG_WARNF("[SMB] Parent path missing for %s, retrying directory creation", parentDir.c_str());

                bool dirReady = createDirectory(parentDir);

                // If directory recovery fails under low memory, reconnect SMB
                // context and retry once to clear any stale libsmb2 state.
                if (!dirReady) {
                    uint32_t maxAllocNow = ESP.getMaxAllocHeap();
                    if (maxAllocNow < 50000) {
                        LOG_WARN("[SMB] Low memory during directory recovery; reconnecting SMB context and retrying once");
                        disconnect();
                        delay(150);
                        if (connect()) {
                            dirReady = createDirectory(parentDir);
                        } else {
                            error = "SMB reconnect failed during directory recovery";
                        }
                    }
                }

                if (dirReady) {
                    lastVerifiedParentDir = parentDir;
                    feedUploadHeartbeat();
                    remoteFile = smb2_open(smb2, fullRemotePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
                    if (remoteFile != nullptr) {
                        LOG_DEBUGF("[SMB] Recovered missing directory path: %s", parentDir.c_str());
                    } else {
                        error = smb2_get_error(smb2);
                    }
                } else {
                    LOG_WARNF("[SMB] Retry directory creation failed for: %s", parentDir.c_str());
                }
            }

            if (remoteFile == nullptr) {
                if (isRecoverableSmbWriteError(errno, error)) {
                    LOG_WARN("[SMB] Open failed with transport/socket state error; disconnecting SMB context");
                    disconnect();
                }

                LOGF("[SMB] ERROR: Failed to open remote file: %s", error);
                LOGF("[SMB] Remote path: %s", fullRemotePath.c_str());
                LOG("[SMB] Possible causes:");
                LOG("[SMB]   - Insufficient permissions on remote share");
                LOG("[SMB]   - Disk full on remote server");
                LOG("[SMB]   - Invalid characters in filename");
                localFile.close();
                return false;
            }
        }

        // Check if upload buffer is allocated
        if (!uploadBuffer) {
            LOG("[SMB] ERROR: No upload buffer allocated");
            LOG("[SMB] Call allocateBuffer() before uploading");
            smb2_close(smb2, remoteFile);
            localFile.close();
            return false;
        }
        feedUploadHeartbeat();

        // Track upload timing and progress
        unsigned long startTime = millis();
        unsigned long lastProgressTime = millis();
        unsigned long lastBytesTransferred = 0;
        const unsigned long PROGRESS_TIMEOUT_MS = 30000;  // 30 seconds without progress

        // Stream file data
        bool success = true;
        bool skipRemoteClose = false;
        bool transportErrorDetected = false;
        unsigned long totalBytesRead = 0;

        while (localFile.available()) {
            // Yield to CPAP if it requested bus access (GPIO33 falling edge ISR)
            if (g_cpapYieldRequest) {
                LOG_WARN("[SMB] CPAP requested bus — suspending upload");
                localFile.close();
                return false;  // Propagates as YIELD_NEEDED to FSM
            }
            size_t bytesRead = localFile.read(uploadBuffer, uploadBufferSize);
            if (bytesRead == 0) {
                // Check if we've read all expected bytes
                if (totalBytesRead < fileSize) {
                    LOGF("[SMB] ERROR: Unexpected end of file, read %lu of %u bytes",
                         totalBytesRead, (unsigned int)fileSize);
                    LOG("[SMB] SD card may have read errors");
                    success = false;
                }
                break;
            }

            totalBytesRead += bytesRead;

            ssize_t bytesWritten = -1;
            int writeErrno = 0;
            const char* writeError = nullptr;

            for (int writeAttempt = 0; writeAttempt <= SMB_WRITE_EAGAIN_RETRIES; ++writeAttempt) {
                bytesWritten = smb2_write(smb2, remoteFile, uploadBuffer, bytesRead);
                if (bytesWritten >= 0) {
                    break;
                }

                writeErrno = errno;
                writeError = smb2_get_error(smb2);

                if (!isTransientSmbSocketBackpressure(writeErrno, writeError) ||
                    writeAttempt == SMB_WRITE_EAGAIN_RETRIES) {
                    break;
                }

                if (writeAttempt == 0 || writeAttempt == SMB_WRITE_EAGAIN_RETRIES - 1) {
                    LOG_WARNF("[SMB] Transient socket backpressure at offset %lu (errno=%d), retrying write (%d/%d)",
                              attemptBytesTransferred,
                              writeErrno,
                              writeAttempt + 1,
                              SMB_WRITE_EAGAIN_RETRIES);
                }

                feedUploadHeartbeat();
                delay(SMB_WRITE_EAGAIN_BASE_DELAY_MS * (writeAttempt + 1));
            }

            if (bytesWritten < 0) {
                const char* error = writeError ? writeError : smb2_get_error(smb2);
                int finalErrno = writeErrno != 0 ? writeErrno : errno;
                LOGF("[SMB] ERROR: Write failed at offset %lu: %s (errno=%d: %s)",
                     attemptBytesTransferred,
                     error ? error : "unknown",
                     finalErrno,
                     strerror(finalErrno));
                LOG("[SMB] Possible causes:");
                LOG("[SMB]   - Network connection lost");
                LOG("[SMB]   - Remote server disk full");
                LOG("[SMB]   - SMB command timeout");

                bool recoverableTransportError = isRecoverableSmbWriteError(finalErrno, error);
                if (recoverableTransportError) {
                    transportErrorDetected = true;
                    skipRemoteClose = true;
                    if (attempt < SMB_UPLOAD_MAX_ATTEMPTS) {
                        shouldRetry = true;
                        LOG_WARN("[SMB] Recoverable SMB transport error detected, will reconnect and retry once");
                    } else {
                        LOG_WARN("[SMB] Recoverable SMB transport error detected but retry budget exhausted");
                    }
                }

                success = false;
                break;
            }

            if ((size_t)bytesWritten != bytesRead) {
                LOGF("[SMB] ERROR: Incomplete write, expected %u bytes, wrote %d", bytesRead, bytesWritten);
                LOG("[SMB] Network may be unstable");
                success = false;
                break;
            }

            attemptBytesTransferred += bytesWritten;

            // Update progress tracking
            if (attemptBytesTransferred > lastBytesTransferred) {
                lastProgressTime = millis();
                lastBytesTransferred = attemptBytesTransferred;
            }

            // Check for progress timeout (stalled upload)
            if (millis() - lastProgressTime > PROGRESS_TIMEOUT_MS) {
                LOGF("[SMB] ERROR: Upload stalled - no progress for %lu seconds", PROGRESS_TIMEOUT_MS / 1000);
                LOG("[SMB] Possible causes:");
                LOG("[SMB]   - Network connection stalled");
                LOG("[SMB]   - SMB server not responding");
                LOG("[SMB]   - Socket write blocked");
                success = false;
                break;
            }

            feedUploadHeartbeat();

            // Print progress for large files (every 1MB)
            if (attemptBytesTransferred % (1024 * 1024) == 0) {
                LOG_DEBUGF("[SMB] Progress: %lu KB / %u KB",
                           attemptBytesTransferred / 1024,
                           (unsigned int)fileSize / 1024);
            }

            // Yield to prevent watchdog timeout on large files
            yield();
        }

        // Verify we transferred all bytes
        if (success && attemptBytesTransferred != fileSize) {
            LOGF("[SMB] ERROR: Size mismatch, transferred %lu bytes, expected %u",
                 attemptBytesTransferred, (unsigned int)fileSize);
            LOG("[SMB] Upload incomplete - file may be corrupted on remote server");
            success = false;
        }

        // Close remote file. If transport is known broken and we are about to
        // reconnect, skip close to avoid another blocking timeout call.
        if (skipRemoteClose) {
            LOG_WARN("[SMB] Skipping smb2_close after recoverable transport failure; forcing reconnect");
        } else if (smb2_close(smb2, remoteFile) < 0) {
            const char* closeError = smb2_get_error(smb2);
            LOGF("[SMB] WARNING: Failed to close remote file: %s", closeError);
            if (isRecoverableSmbWriteError(errno, closeError)) {
                transportErrorDetected = true;
            }
            // Don't fail the upload if close fails - data was already written
        }

        localFile.close();
        feedUploadHeartbeat();

        unsigned long uploadTime = millis() - startTime;

        if (success) {
            bytesTransferred = attemptBytesTransferred;
            float transferRate = uploadTime > 0 ? (attemptBytesTransferred / 1024.0f) / (uploadTime / 1000.0f) : 0.0f;
            LOGF("[SMB] Upload complete: %lu bytes in %lu ms (%.2f KB/s)",
                 attemptBytesTransferred, uploadTime, transferRate);
            LOG_DEBUGF("[SMB] File size verification: SD=%u bytes, Transferred=%lu bytes, Match=%s",
                       (unsigned int)fileSize,
                       attemptBytesTransferred,
                       (attemptBytesTransferred == fileSize) ? "YES" : "NO");
            return true;
        }

        bytesTransferred = attemptBytesTransferred;
        LOGF("[SMB] Upload failed - Expected %u bytes, transferred %lu bytes",
             (unsigned int)fileSize,
             attemptBytesTransferred);

        if (transportErrorDetected) {
            LOG_WARN("[SMB] Forcing SMB disconnect after transport failure to avoid stale socket reuse");
            disconnect();
        }

        if (!shouldRetry) {
            return false;
        }

        LOG_WARN("[SMB] Reconnecting SMB context after recoverable transport error...");
        disconnect();
        delay(150);
        if (!connect()) {
            LOG_WARN("[SMB] Initial reconnect failed after transport error");

            bool smbReconnected = false;
            if (recoverWiFiAfterSmbTransportFailure()) {
                // Give network stack a brief settle window after WiFi reconnect,
                // then try SMB connect a few times before giving up.
                const int SMB_RECONNECT_ATTEMPTS_AFTER_WIFI = 3;
                for (int reconnectAttempt = 1;
                     reconnectAttempt <= SMB_RECONNECT_ATTEMPTS_AFTER_WIFI;
                     ++reconnectAttempt) {
                    if (reconnectAttempt > 1) {
                        LOG_WARNF("[SMB] SMB reconnect retry %d/%d after WiFi recovery",
                                  reconnectAttempt,
                                  SMB_RECONNECT_ATTEMPTS_AFTER_WIFI);
                    }

                    delay(200 * reconnectAttempt);
                    if (connect()) {
                        smbReconnected = true;
                        break;
                    }
                    feedUploadHeartbeat();
                }
            }

            if (smbReconnected) {
                feedUploadHeartbeat();
                continue;
            }

            LOG_ERROR("[SMB] Reconnect failed - cannot retry upload");
            return false;
        }
        feedUploadHeartbeat();
    }

    return false;
}

int SMBUploader::countRemoteFiles(const String& remotePath) {
    if (!connected) {
        LOG("[SMB] ERROR: Not connected - cannot scan remote directory");
        return -1;
    }
    
    // Prepend base path if configured
    String fullRemotePath = remotePath;
    if (!smbBasePath.isEmpty()) {
        // Remove leading slash from remotePath if present
        String cleanRemotePath = remotePath;
        if (cleanRemotePath.startsWith("/")) {
            cleanRemotePath = cleanRemotePath.substring(1);
        }
        fullRemotePath = smbBasePath + "/" + cleanRemotePath;
    } else if (fullRemotePath.startsWith("/")) {
        // Remove leading slash for libsmb2 compatibility
        fullRemotePath = fullRemotePath.substring(1);
    }
    
    LOG_DEBUGF("[SMB] Scanning remote directory: %s", fullRemotePath.c_str());
    
    // Check if directory exists
    struct smb2_stat_64 st;
    int stat_result = smb2_stat(smb2, fullRemotePath.c_str(), &st);
    if (stat_result < 0) {
        const char* error = smb2_get_error(smb2);
        LOG_DEBUGF("[SMB] Directory does not exist or cannot access: %s (%s)", fullRemotePath.c_str(), error);
        return 0;  // Directory doesn't exist, so 0 files
    }
    
    if (st.smb2_type != SMB2_TYPE_DIRECTORY) {
        LOG_DEBUGF("[SMB] Path exists but is not a directory: %s", fullRemotePath.c_str());
        return -1;  // Error - path exists but is not a directory
    }
    
    // Open directory for reading
    struct smb2dir* dir = smb2_opendir(smb2, fullRemotePath.c_str());
    if (dir == nullptr) {
        const char* error = smb2_get_error(smb2);
        LOG_DEBUGF("[SMB] Failed to open directory: %s (%s)", fullRemotePath.c_str(), error);
        return -1;
    }
    
    // Count files (not directories)
    int fileCount = 0;
    struct smb2dirent* entry;
    
    while ((entry = smb2_readdir(smb2, dir)) != nullptr) {
        // Skip "." and ".." entries
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
            continue;
        }
        
        // Only count regular files, not directories
        if (entry->st.smb2_type == SMB2_TYPE_FILE) {
            fileCount++;
        }
    }
    
    // Close directory
    smb2_closedir(smb2, dir);
    
    LOG_DEBUGF("[SMB] Found %d files in remote directory: %s", fileCount, fullRemotePath.c_str());
    return fileCount;
}

bool SMBUploader::getRemoteFileInfo(const String& remotePath, std::map<String, size_t>& fileInfo) {
    if (!connected) {
        LOG("[SMB] ERROR: Not connected - cannot scan remote directory");
        return false;
    }
    
    // Clear the output map
    fileInfo.clear();
    
    // Prepend base path if configured
    String fullRemotePath = remotePath;
    if (!smbBasePath.isEmpty()) {
        // Remove leading slash from remotePath if present
        String cleanRemotePath = remotePath;
        if (cleanRemotePath.startsWith("/")) {
            cleanRemotePath = cleanRemotePath.substring(1);
        }
        fullRemotePath = smbBasePath + "/" + cleanRemotePath;
    } else if (fullRemotePath.startsWith("/")) {
        // Remove leading slash for libsmb2 compatibility
        fullRemotePath = fullRemotePath.substring(1);
    }
    
    LOG_DEBUGF("[SMB] Getting file info from remote directory: %s", fullRemotePath.c_str());
    
    // Check if directory exists
    struct smb2_stat_64 st;
    int stat_result = smb2_stat(smb2, fullRemotePath.c_str(), &st);
    if (stat_result < 0) {
        const char* error = smb2_get_error(smb2);
        LOG_DEBUGF("[SMB] Directory does not exist or cannot access: %s (%s)", fullRemotePath.c_str(), error);
        return true;  // Directory doesn't exist, return empty map (success)
    }
    
    if (st.smb2_type != SMB2_TYPE_DIRECTORY) {
        LOG_DEBUGF("[SMB] Path exists but is not a directory: %s", fullRemotePath.c_str());
        return false;  // Error - path exists but is not a directory
    }
    
    // Open directory for reading
    struct smb2dir* dir = smb2_opendir(smb2, fullRemotePath.c_str());
    if (dir == nullptr) {
        const char* error = smb2_get_error(smb2);
        LOG_DEBUGF("[SMB] Failed to open directory: %s (%s)", fullRemotePath.c_str(), error);
        return false;
    }
    
    // Collect file information
    struct smb2dirent* entry;
    
    while ((entry = smb2_readdir(smb2, dir)) != nullptr) {
        // Skip "." and ".." entries
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
            continue;
        }
        
        // Only process regular files, not directories
        if (entry->st.smb2_type == SMB2_TYPE_FILE) {
            String filename = String(entry->name);
            size_t fileSize = (size_t)entry->st.smb2_size;
            fileInfo[filename] = fileSize;
            LOG_DEBUGF("[SMB] Remote file: %s (%u bytes)", filename.c_str(), fileSize);
        }
    }
    
    // Close directory
    smb2_closedir(smb2, dir);
    
    LOG_DEBUGF("[SMB] Collected info for %d files in remote directory: %s", fileInfo.size(), fullRemotePath.c_str());
    return true;
}

#endif // ENABLE_SMB_UPLOAD
