#include "SMBUploader.h"
#include "Logger.h"

#ifdef ENABLE_SMB_UPLOAD

#include <fcntl.h>  // For O_WRONLY, O_CREAT, O_TRUNC flags

// Include libsmb2 headers
// Note: These will be available when libsmb2 is added as ESP-IDF component
extern "C" {
    #include "smb2/smb2.h"
    #include "smb2/libsmb2.h"
}

// Buffer size for file streaming (32KB for good performance)
#define UPLOAD_BUFFER_SIZE 32768

SMBUploader::SMBUploader(const String& endpoint, const String& user, const String& password)
    : smbUser(user), smbPassword(password), smb2(nullptr), connected(false) {
    parseEndpoint(endpoint);
}

SMBUploader::~SMBUploader() {
    end();
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
        LOG("[SMB] Check ENDPOINT configuration in config.json");
        return false;
    }
    
    // Create SMB2 context
    smb2 = smb2_init_context();
    if (smb2 == nullptr) {
        LOG("[SMB] ERROR: Failed to initialize SMB context");
        LOG("[SMB] This may indicate a memory allocation failure");
        return false;
    }
    
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
    if (smb2 != nullptr) {
        if (connected) {
            smb2_disconnect_share(smb2);
            connected = false;
        }
        smb2_destroy_context(smb2);
        smb2 = nullptr;
    }
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
        // Stat failed - directory might not exist or we might not have permissions
        LOG_DEBUGF("[SMB] Directory stat failed for %s (will attempt to create)", cleanPath.c_str());
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
    LOG_DEBUGF("[SMB] Creating directory: %s", cleanPath.c_str());
    
    int mkdir_result = smb2_mkdir(smb2, cleanPath.c_str());
    if (mkdir_result < 0) {
        const char* error = smb2_get_error(smb2);
        
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
    
    LOG_DEBUGF("[SMB] Directory created successfully: %s", cleanPath.c_str());
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
    
    LOG_DEBUGF("[SMB] Uploading %s (%u bytes)", localPath.c_str(), fileSize);
    LOG_DEBUGF("[SMB] Remote path: %s", fullRemotePath.c_str());
    
    // Ensure parent directory exists
    int lastSlash = fullRemotePath.lastIndexOf('/');
    if (lastSlash > 0) {
        String parentDir = fullRemotePath.substring(0, lastSlash);
        if (!createDirectory(parentDir)) {
            LOGF("[SMB] ERROR: Failed to create parent directory: %s", parentDir.c_str());
            LOG("[SMB] Check permissions on remote share");
            localFile.close();
            return false;
        }
    }
    
    // Open remote file for writing
    // Convert Arduino String to C string for libsmb2
    struct smb2fh* remoteFile = smb2_open(smb2, fullRemotePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (remoteFile == nullptr) {
        const char* error = smb2_get_error(smb2);
        LOGF("[SMB] ERROR: Failed to open remote file: %s", error);
        LOGF("[SMB] Remote path: %s", fullRemotePath.c_str());
        LOG("[SMB] Possible causes:");
        LOG("[SMB]   - Insufficient permissions on remote share");
        LOG("[SMB]   - Disk full on remote server");
        LOG("[SMB]   - Invalid characters in filename");
        localFile.close();
        return false;
    }
    
    // Allocate buffer for streaming
    uint8_t* buffer = (uint8_t*)malloc(UPLOAD_BUFFER_SIZE);
    if (buffer == nullptr) {
        LOG("[SMB] ERROR: Failed to allocate upload buffer");
        LOG("[SMB] System may be low on memory");
        smb2_close(smb2, remoteFile);
        localFile.close();
        return false;
    }
    
    // Track upload timing
    unsigned long startTime = millis();
    
    // Stream file data
    bool success = true;
    unsigned long totalBytesRead = 0;
    
    while (localFile.available()) {
        size_t bytesRead = localFile.read(buffer, UPLOAD_BUFFER_SIZE);
        if (bytesRead == 0) {
            // Check if we've read all expected bytes
            if (totalBytesRead < fileSize) {
                LOGF("[SMB] ERROR: Unexpected end of file, read %lu of %u bytes", totalBytesRead, fileSize);
                LOG("[SMB] SD card may have read errors");
                success = false;
            }
            break;
        }
        
        totalBytesRead += bytesRead;
        
        ssize_t bytesWritten = smb2_write(smb2, remoteFile, buffer, bytesRead);
        if (bytesWritten < 0) {
            const char* error = smb2_get_error(smb2);
            LOGF("[SMB] ERROR: Write failed at offset %lu: %s", bytesTransferred, error);
            LOG("[SMB] Possible causes:");
            LOG("[SMB]   - Network connection lost");
            LOG("[SMB]   - Remote server disk full");
            LOG("[SMB]   - SMB session timeout");
            success = false;
            break;
        }
        
        if ((size_t)bytesWritten != bytesRead) {
            LOGF("[SMB] ERROR: Incomplete write, expected %u bytes, wrote %d", bytesRead, bytesWritten);
            LOG("[SMB] Network may be unstable");
            success = false;
            break;
        }
        
        bytesTransferred += bytesWritten;
        
        // Print progress for large files (every 1MB)
        if (bytesTransferred % (1024 * 1024) == 0) {
            LOG_DEBUGF("[SMB] Progress: %lu KB / %u KB", bytesTransferred / 1024, fileSize / 1024);
        }
        
        // Yield to prevent watchdog timeout on large files
        yield();
    }
    
    // Verify we transferred all bytes
    if (success && bytesTransferred != fileSize) {
        LOGF("[SMB] ERROR: Size mismatch, transferred %lu bytes, expected %u", bytesTransferred, fileSize);
        LOG("[SMB] Upload incomplete - file may be corrupted on remote server");
        success = false;
    }
    
    // Cleanup
    free(buffer);
    
    // Close remote file
    if (smb2_close(smb2, remoteFile) < 0) {
        LOGF("[SMB] WARNING: Failed to close remote file: %s", smb2_get_error(smb2));
        // Don't fail the upload if close fails - data was already written
    }
    
    localFile.close();
    
    unsigned long uploadTime = millis() - startTime;
    
    if (success) {
        float transferRate = uploadTime > 0 ? (bytesTransferred / 1024.0) / (uploadTime / 1000.0) : 0.0;
        LOGF("[SMB] Upload complete: %lu bytes in %lu ms (%.2f KB/s)", 
             bytesTransferred, uploadTime, transferRate);
        LOG_DEBUGF("[SMB] File size verification: SD=%u bytes, Transferred=%lu bytes, Match=%s",
             fileSize, bytesTransferred, (bytesTransferred == fileSize) ? "YES" : "NO");
        
        if (bytesTransferred != fileSize) {
            LOG("[SMB] ERROR: Size mismatch detected! File may be corrupted on remote server");
        }
    } else {
        LOGF("[SMB] Upload failed - Expected %u bytes, transferred %lu bytes", 
             fileSize, bytesTransferred);
    }
    
    return success;
}

#endif // ENABLE_SMB_UPLOAD
