#include "SleepHQUploader.h"
#include "Logger.h"

#ifdef ENABLE_SLEEPHQ_UPLOAD

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "NetworkRecovery.h"
#include "TrafficMonitor.h"
#include <esp32/rom/md5_hash.h>
#include <esp_task_wdt.h>

// GTS Root R4 - Google Trust Services root CA certificate (expires June 22, 2036)
// Used for TLS validation of sleephq.com (which uses Google Trust Services)
// If sleephq.com changes CA provider, set CLOUD_INSECURE_TLS=true as fallback
static const char* GTS_ROOT_R4_CA = \
    "-----BEGIN CERTIFICATE-----\n" \
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n" \
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n" \
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n" \
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n" \
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n" \
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n" \
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n" \
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n" \
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n" \
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n" \
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n" \
    "-----END CERTIFICATE-----\n";

// Upload buffer size for streaming files
#define CLOUD_UPLOAD_BUFFER_SIZE 4096

SleepHQUploader::SleepHQUploader(Config* cfg)
    : config(cfg),
      tokenObtainedAt(0),
      tokenExpiresIn(0),
      connected(false),
      lowMemoryKeepAliveWarned(false),
      tlsClient(nullptr) {
}

SleepHQUploader::~SleepHQUploader() {
    end();
    if (tlsClient) {
        delete tlsClient;
        tlsClient = nullptr;
    }
}

void SleepHQUploader::setupTLS() {
    if (!tlsClient) {
        tlsClient = new WiFiClientSecure();
        if (!tlsClient) {
            LOG_ERROR("[SleepHQ] Failed to allocate WiFiClientSecure - OOM!");
            return;
        }
    }
    configureTLS();
}

void SleepHQUploader::resetTLS() {
    if (tlsClient) {
        tlsClient->stop();
        delay(100);  // Let lwIP process the FIN/RST before freeing
        delete tlsClient;
        tlsClient = nullptr;
    }
    // Give lwIP TCP stack time to release socket FDs and clean up TIME_WAIT
    delay(500);
    setupTLS();
}

void SleepHQUploader::configureTLS() {
    if (!tlsClient) return;
    
    if (config->getCloudInsecureTls()) {
        LOG_WARN("[SleepHQ] TLS certificate validation DISABLED (insecure mode)");
        tlsClient->setInsecure();
    } else {
        LOG_DEBUG("[SleepHQ] Using GTS Root R4 CA certificate for TLS validation");
        tlsClient->setCACert(GTS_ROOT_R4_CA);
    }
    
    // Set reasonable timeout for ESP32 - increased to 60s for slow networks
    tlsClient->setTimeout(60);  // 60 seconds
}

void SleepHQUploader::resetConnection() {
    resetTLS();
}

bool SleepHQUploader::begin() {
    LOG("[SleepHQ] Initializing cloud uploader...");
    
    // Setup TLS
    setupTLS();
    if (!tlsClient) {
        LOG_ERROR("[SleepHQ] Failed to initialize TLS client");
        return false;
    }
    
    // Authenticate
    if (!authenticate()) {
        LOG_ERROR("[SleepHQ] Authentication failed");
        return false;
    }
    
    // Discover team_id if not configured
    String configTeamId = config->getCloudTeamId();
    if (!configTeamId.isEmpty()) {
        teamId = configTeamId;
        LOGF("[SleepHQ] Using configured team ID: %s", teamId.c_str());
    } else {
        if (!discoverTeamId()) {
            LOG_ERROR("[SleepHQ] Failed to discover team ID");
            return false;
        }
    }
    
    // Create the import session while the TLS connection is still alive from
    // the OAuth and team-discovery requests above.  This avoids a second
    // SSL handshake (which would fail at low max_alloc) when ensureCloudImport()
    // calls createImport() later.
    if (!createImport()) {
        LOG_ERROR("[SleepHQ] Failed to create initial import session");
        return false;
    }

    connected = true;
    LOG("[SleepHQ] Cloud uploader initialized successfully");
    return true;
}

bool SleepHQUploader::authenticate() {
    LOG("[SleepHQ] Authenticating with OAuth...");
    
    String baseUrl = config->getCloudBaseUrl();
    String tokenPath = "/oauth/token";
    
    // Build OAuth request body
    String body = "grant_type=password";
    body += "&client_id=" + config->getCloudClientId();
    body += "&client_secret=" + config->getCloudClientSecret();
    body += "&scope=read+write";
    
    String responseBody;
    int httpCode;
    
    if (!httpRequest("POST", tokenPath, body, "application/x-www-form-urlencoded", responseBody, httpCode)) {
        LOG_ERROR("[SleepHQ] OAuth request failed");
        return false;
    }
    
    if (httpCode != 200) {
        LOG_ERRORF("[SleepHQ] OAuth failed with HTTP %d", httpCode);
        LOG_ERRORF("[SleepHQ] Response: %s", responseBody.c_str());
        return false;
    }
    
    // Parse response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, responseBody);
    if (error) {
        LOG_ERRORF("[SleepHQ] Failed to parse OAuth response: %s", error.c_str());
        return false;
    }
    
    accessToken = doc["access_token"].as<String>();
    tokenExpiresIn = doc["expires_in"] | 7200;
    tokenObtainedAt = millis();
    
    if (accessToken.isEmpty()) {
        LOG_ERROR("[SleepHQ] No access token in OAuth response");
        return false;
    }
    
    LOGF("[SleepHQ] Authenticated successfully (token expires in %lu seconds)", tokenExpiresIn);
    return true;
}

bool SleepHQUploader::ensureAccessToken() {
    if (accessToken.isEmpty()) {
        return authenticate();
    }
    
    // Check if token has expired (with 60 second safety margin)
    unsigned long elapsed = (millis() - tokenObtainedAt) / 1000;
    if (tokenExpiresIn <= 60 || elapsed >= (tokenExpiresIn - 60)) {
        LOG("[SleepHQ] Access token expired, re-authenticating...");
        return authenticate();
    }
    
    return true;
}

bool SleepHQUploader::discoverTeamId() {
    LOG("[SleepHQ] Discovering team ID...");
    
    String responseBody;
    int httpCode;
    
    if (!httpRequest("GET", "/api/v1/me", "", "", responseBody, httpCode)) {
        LOG_ERROR("[SleepHQ] Failed to request /api/v1/me");
        return false;
    }
    
    if (httpCode != 200) {
        LOG_ERRORF("[SleepHQ] /api/v1/me failed with HTTP %d", httpCode);
        return false;
    }
    
    // Parse response to get current_team_id
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, responseBody);
    if (error) {
        LOG_ERRORF("[SleepHQ] Failed to parse /me response: %s", error.c_str());
        return false;
    }
    
    // Try data.attributes.current_team_id or data.current_team_id
    long teamIdVal = 0;
    if (doc.containsKey("data")) {
        JsonObject data = doc["data"];
        if (data.containsKey("attributes")) {
            teamIdVal = data["attributes"]["current_team_id"] | 0L;
        }
        if (teamIdVal == 0) {
            teamIdVal = data["current_team_id"] | 0L;
        }
    }
    
    if (teamIdVal == 0) {
        LOG_ERROR("[SleepHQ] Could not find current_team_id in /me response");
        LOG_DEBUG("[SleepHQ] Response body:");
        LOG_DEBUG(responseBody.c_str());
        return false;
    }
    
    teamId = String(teamIdVal);
    LOGF("[SleepHQ] Discovered team ID: %s", teamId.c_str());
    return true;
}

bool SleepHQUploader::createImport() {
    if (!ensureAccessToken()) {
        return false;
    }
    
    LOG("[SleepHQ] Creating new import session...");
    
    String path = "/api/v1/teams/" + teamId + "/imports";
    int deviceId = config->getCloudDeviceId();
    
    // Try raw TLS if connection is alive (avoids HTTPClient forcing SSL reconnection)
    if (tlsClient && tlsClient->connected()) {
        char hdrBuf[256];
        int n;
        
        // Build body on stack
        char bodyBuf[64];
        int bodyLen = 0;
        if (deviceId > 0) {
            bodyLen = snprintf(bodyBuf, sizeof(bodyBuf), "device_id=%d", deviceId);
        }
        
        // Send request
        n = snprintf(hdrBuf, sizeof(hdrBuf), "POST %s HTTP/1.1\r\n", path.c_str());
        tlsClient->write((const uint8_t*)hdrBuf, n);
        
        const char* bu = config->getCloudBaseUrl().c_str();
        const char* hs = strstr(bu, "://");
        const char* host = hs ? hs + 3 : bu;
        n = snprintf(hdrBuf, sizeof(hdrBuf), "Host: %s\r\n", host);
        tlsClient->write((const uint8_t*)hdrBuf, n);
        
        tlsClient->write((const uint8_t*)"Authorization: Bearer ", 22);
        tlsClient->write((const uint8_t*)accessToken.c_str(), accessToken.length());
        
        if (bodyLen > 0) {
            n = snprintf(hdrBuf, sizeof(hdrBuf),
                "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n", bodyLen);
        } else {
            n = snprintf(hdrBuf, sizeof(hdrBuf),
                "\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n");
        }
        tlsClient->write((const uint8_t*)hdrBuf, n);
        if (bodyLen > 0) {
            tlsClient->write((const uint8_t*)bodyBuf, bodyLen);
        }
        tlsClient->flush();
        
        // Wait for response
        unsigned long timeout = millis() + 15000;
        while (!tlsClient->available() && millis() < timeout) {
            delay(10);
        }
        
        if (tlsClient->available()) {
            // Read status line
            char lineBuf[256];
            int lineLen = 0;
            unsigned long ld = millis() + 5000;
            while (millis() < ld) {
                if (!tlsClient->available()) { delay(2); continue; }
                int c = tlsClient->read();
                if (c < 0 || c == '\n') break;
                if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
            }
            lineBuf[lineLen] = '\0';
            const char* sp = strchr(lineBuf, ' ');
            int rawHttpCode = sp ? atoi(sp + 1) : -1;
            
            // Parse headers
            long contentLength = -1;
            bool isChunked = false;
            unsigned long headerDl = millis() + 5000;
            while (millis() < headerDl) {
                if (!tlsClient->available()) { delay(2); continue; }
                lineLen = 0;
                while (millis() < headerDl) {
                    if (!tlsClient->available()) { delay(2); continue; }
                    int c = tlsClient->read();
                    if (c < 0 || c == '\n') break;
                    if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                }
                lineBuf[lineLen] = '\0';
                if (lineLen == 0) break;
                if (strncasecmp(lineBuf, "Content-Length:", 15) == 0) {
                    contentLength = atol(lineBuf + 15);
                } else if (strncasecmp(lineBuf, "Transfer-Encoding:", 18) == 0) {
                    for (int ci = 18; lineBuf[ci]; ci++) lineBuf[ci] = tolower((unsigned char)lineBuf[ci]);
                    if (strstr(lineBuf + 18, "chunked")) isChunked = true;
                }
            }
            
            // Read body into stack buffer
            char respBuf[1024];
            int respLen = 0;
            if (isChunked) {
                unsigned long chunkDl = millis() + 5000;
                while (millis() < chunkDl) {
                    if (!tlsClient->available()) { delay(2); continue; }
                    lineLen = 0;
                    while (millis() < chunkDl) {
                        if (!tlsClient->available()) { delay(2); continue; }
                        int c = tlsClient->read();
                        if (c < 0 || c == '\n') break;
                        if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                    }
                    lineBuf[lineLen] = '\0';
                    long chunkSize = strtol(lineBuf, NULL, 16);
                    if (chunkSize <= 0) {
                        // Drain end chunk trailers
                        unsigned long trailerDl = millis() + 2000;
                        while (tlsClient->available() && millis() < trailerDl) {
                            int c = tlsClient->read();
                            if (c == '\n' || c < 0) break;
                        }
                        break;
                    }
                    long remaining = chunkSize;
                    while (remaining > 0 && millis() < chunkDl) {
                        if (!tlsClient->available()) { delay(2); continue; }
                        int c = tlsClient->read();
                        if (c < 0) break;
                        if (respLen < (int)sizeof(respBuf) - 1) respBuf[respLen++] = (char)c;
                        remaining--;
                    }
                    // Trailing CRLF
                    while (tlsClient->available()) {
                        int c = tlsClient->read();
                        if (c == '\n' || c < 0) break;
                    }
                }
            } else if (contentLength > 0) {
                long remaining = contentLength;
                unsigned long bodyDl = millis() + 5000;
                while (remaining > 0 && millis() < bodyDl) {
                    if (!tlsClient->available()) { delay(2); continue; }
                    int c = tlsClient->read();
                    if (c < 0) break;
                    if (respLen < (int)sizeof(respBuf) - 1) respBuf[respLen++] = (char)c;
                    remaining--;
                }
            } else {
                unsigned long bodyDl = millis() + 2000;
                while (millis() < bodyDl) {
                    if (!tlsClient->available()) { delay(2); if (!tlsClient->connected()) break; continue; }
                    int c = tlsClient->read();
                    if (c < 0) break;
                    if (respLen < (int)sizeof(respBuf) - 1) respBuf[respLen++] = (char)c;
                    bodyDl = millis() + 500;
                }
            }
            respBuf[respLen] = '\0';
            
            if (rawHttpCode == 201 || rawHttpCode == 200) {
                // Parse import_id from JSON response
                DynamicJsonDocument doc(2048);
                DeserializationError err = deserializeJson(doc, respBuf);
                if (!err) {
                    long importIdVal = 0;
                    if (doc.containsKey("data")) {
                        JsonObject data = doc["data"];
                        if (data.containsKey("attributes")) {
                            importIdVal = data["attributes"]["id"] | 0L;
                        }
                        if (importIdVal == 0) {
                            importIdVal = data["id"] | 0L;
                        }
                    }
                    if (importIdVal > 0) {
                        currentImportId = String(importIdVal);
                        LOGF("[SleepHQ] Import created: %s (raw TLS)", currentImportId.c_str());
                        extern volatile unsigned long g_uploadHeartbeat;
                        g_uploadHeartbeat = millis();
                        return true;
                    }
                }
                LOG_ERROR("[SleepHQ] Failed to parse import ID from raw TLS response");
                return false;
            }
            LOG_ERRORF("[SleepHQ] Create import failed with HTTP %d (raw TLS)", rawHttpCode);
            return false;
        }
        LOG_WARN("[SleepHQ] Raw TLS createImport timed out, falling back to HTTPClient");
    }
    
    // Fall back to HTTPClient (needs new SSL connection)
    String body = "";
    if (deviceId > 0) {
        body = "device_id=" + String(deviceId);
    }
    
    String responseBody;
    int httpCode;
    
    if (!httpRequest("POST", path, body, 
                     body.isEmpty() ? "" : "application/x-www-form-urlencoded",
                     responseBody, httpCode)) {
        LOG_ERROR("[SleepHQ] Failed to create import");
        return false;
    }
    
    if (httpCode != 201 && httpCode != 200) {
        LOG_ERRORF("[SleepHQ] Create import failed with HTTP %d", httpCode);
        LOG_ERRORF("[SleepHQ] Response: %s", responseBody.c_str());
        return false;
    }
    
    // Parse import_id from response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, responseBody);
    if (error) {
        LOG_ERRORF("[SleepHQ] Failed to parse import response: %s", error.c_str());
        return false;
    }
    
    long importIdVal = 0;
    if (doc.containsKey("data")) {
        JsonObject data = doc["data"];
        if (data.containsKey("attributes")) {
            importIdVal = data["attributes"]["id"] | 0L;
        }
        if (importIdVal == 0) {
            importIdVal = data["id"] | 0L;
        }
    }
    
    if (importIdVal == 0) {
        LOG_ERROR("[SleepHQ] Could not find import ID in response");
        return false;
    }
    
    currentImportId = String(importIdVal);
    LOGF("[SleepHQ] Import created: %s", currentImportId.c_str());
    return true;
}

bool SleepHQUploader::processImport() {
    if (currentImportId.isEmpty()) {
        LOG_WARN("[SleepHQ] No active import to process");
        return true;
    }
    
    if (!ensureAccessToken()) {
        return false;
    }
    
    LOG("[SleepHQ] Processing import...");
    
    String path = "/api/v1/imports/" + currentImportId + "/process_files";
    
    // Try raw TLS if connection is alive (avoids new SSL handshake at low max_alloc)
    if (tlsClient && tlsClient->connected()) {
        char hdrBuf[256];
        int n;
        
        n = snprintf(hdrBuf, sizeof(hdrBuf), "POST %s HTTP/1.1\r\n", path.c_str());
        tlsClient->write((const uint8_t*)hdrBuf, n);
        
        const char* bu = config->getCloudBaseUrl().c_str();
        const char* hs = strstr(bu, "://");
        const char* host = hs ? hs + 3 : bu;
        n = snprintf(hdrBuf, sizeof(hdrBuf), "Host: %s\r\n", host);
        tlsClient->write((const uint8_t*)hdrBuf, n);
        
        // Write auth header in parts to handle long tokens
        tlsClient->write((const uint8_t*)"Authorization: Bearer ", 22);
        tlsClient->write((const uint8_t*)accessToken.c_str(), accessToken.length());
        tlsClient->write((const uint8_t*)"\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n", 47);
        tlsClient->flush();
        
        // Wait for response
        unsigned long timeout = millis() + 15000;
        while (!tlsClient->available() && millis() < timeout) {
            delay(10);
        }
        
        if (tlsClient->available()) {
            // Read status line — stack buffer
            char lineBuf[256];
            int lineLen = 0;
            unsigned long ld = millis() + 5000;
            while (millis() < ld) {
                if (!tlsClient->available()) { delay(2); continue; }
                int c = tlsClient->read();
                if (c < 0 || c == '\n') break;
                if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
            }
            lineBuf[lineLen] = '\0';
            
            const char* sp = strchr(lineBuf, ' ');
            int httpCode = sp ? atoi(sp + 1) : -1;
            
            // Drain headers and body
            bool draining = true;
            bool inBody = false;
            long contentLength = -1;
            unsigned long drainDl = millis() + 5000;
            while (draining && millis() < drainDl) {
                if (!tlsClient->available()) {
                    delay(2);
                    if (!tlsClient->connected()) break;
                    continue;
                }
                if (!inBody) {
                    lineLen = 0;
                    while (millis() < drainDl) {
                        if (!tlsClient->available()) { delay(2); continue; }
                        int c = tlsClient->read();
                        if (c < 0 || c == '\n') break;
                        if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                    }
                    lineBuf[lineLen] = '\0';
                    if (lineLen == 0) {
                        inBody = true;
                        continue;
                    }
                    if (strncasecmp(lineBuf, "Content-Length:", 15) == 0) {
                        contentLength = atol(lineBuf + 15);
                    }
                } else {
                    // Drain body
                    if (contentLength > 0) {
                        uint8_t tmp[256];
                        size_t toRead = (contentLength < (long)sizeof(tmp)) ? contentLength : sizeof(tmp);
                        size_t r = tlsClient->read(tmp, toRead);
                        contentLength -= r;
                        if (contentLength <= 0) draining = false;
                    } else {
                        // Drain briefly
                        while (tlsClient->available()) tlsClient->read();
                        draining = false;
                    }
                }
            }
            
            if (httpCode == 200 || httpCode == 201) {
                LOGF("[SleepHQ] Import %s submitted for processing", currentImportId.c_str());
                currentImportId = "";
                extern volatile unsigned long g_uploadHeartbeat;
                g_uploadHeartbeat = millis();
                return true;
            }
            LOG_ERRORF("[SleepHQ] Process import failed with HTTP %d (raw TLS)", httpCode);
            currentImportId = "";
            return false;
        }
        LOG_WARN("[SleepHQ] Raw TLS processImport timed out, falling back to HTTPClient");
    }
    
    // Fall back to HTTPClient (needs new SSL connection)
    String responseBody;
    int httpCode;
    
    if (!httpRequest("POST", path, "", "", responseBody, httpCode)) {
        LOG_ERROR("[SleepHQ] Failed to process import");
        return false;
    }
    
    if (httpCode != 201 && httpCode != 200) {
        LOG_ERRORF("[SleepHQ] Process import failed with HTTP %d", httpCode);
        return false;
    }
    
    LOGF("[SleepHQ] Import %s submitted for processing", currentImportId.c_str());
    currentImportId = "";
    return true;
}

String SleepHQUploader::computeContentHash(fs::FS &sd, const String& localPath, const String& fileName,
                                             unsigned long& hashedSize) {
    // SleepHQ content_hash = MD5(file_content + filename)
    // Note: filename only (no path)
    // hashedSize returns the exact byte count that was hashed, so the upload
    // can read the same number of bytes ("size-locked" to avoid hash mismatch
    // when the CPAP machine appends data between hash and upload).
    
    File file = sd.open(localPath, FILE_READ);
    if (!file) {
        LOG_ERRORF("[SleepHQ] Cannot open file for hashing: %s", localPath.c_str());
        hashedSize = 0;
        return "";
    }
    
    // Snapshot file size at open time - only hash this many bytes
    unsigned long snapshotSize = file.size();
    
    struct MD5Context md5ctx;
    MD5Init(&md5ctx);
    
    // Hash exactly snapshotSize bytes (not file.available() which can grow)
    uint8_t buffer[CLOUD_UPLOAD_BUFFER_SIZE];
    unsigned long totalHashed = 0;
    while (totalHashed < snapshotSize) {
        size_t toRead = sizeof(buffer);
        if (snapshotSize - totalHashed < toRead) {
            toRead = snapshotSize - totalHashed;
        }
        size_t bytesRead = file.read(buffer, toRead);
        if (bytesRead == 0) break;  // EOF or read error
        MD5Update(&md5ctx, buffer, bytesRead);
        totalHashed += bytesRead;
    }
    file.close();
    hashedSize = totalHashed;
    
    // Append filename to hash
    MD5Update(&md5ctx, (const uint8_t*)fileName.c_str(), fileName.length());
    
    // Finalize
    uint8_t digest[16];
    MD5Final(digest, &md5ctx);
    
    // Convert to hex string
    char hashStr[33];
    for (int i = 0; i < 16; i++) {
        sprintf(hashStr + (i * 2), "%02x", digest[i]);
    }
    hashStr[32] = '\0';
    
    return String(hashStr);
}

bool SleepHQUploader::upload(const String& localPath, const String& remotePath,
                              fs::FS &sd, unsigned long& bytesTransferred, String& fileChecksum) {
    bytesTransferred = 0;
    
    if (!ensureAccessToken()) {
        return false;
    }
    
    if (currentImportId.isEmpty()) {
        LOG_ERROR("[SleepHQ] No active import - call createImport() first");
        return false;
    }
    
    // Extract filename from path
    String fileName = localPath;
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash >= 0) {
        fileName = fileName.substring(lastSlash + 1);
    }
    
    // Compute content hash (returns the exact byte count hashed for size-locked upload)
    unsigned long lockedFileSize = 0;
    String contentHash = computeContentHash(sd, localPath, fileName, lockedFileSize);
    if (contentHash.isEmpty()) {
        return false;
    }
    
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t maxAlloc = ESP.getMaxAllocHeap();
    
    LOG_DEBUGF("[SleepHQ] Uploading: %s (%lu bytes, hash: %s, free: %u, max_alloc: %u)",
               localPath.c_str(), lockedFileSize, contentHash.c_str(), freeHeap, maxAlloc);
    
    // Prefer one persistent TLS session across the entire import.
    // Reconnecting per large file increases TLS handshake churn and heap fragmentation risk.
    bool lowMemory = (maxAlloc < 50000); // 50KB threshold (SSL reconnect often needs ~40KB contiguous)
    if (!lowMemory) {
        lowMemoryKeepAliveWarned = false;
    } else if (!lowMemoryKeepAliveWarned) {
        LOG_WARNF("[SleepHQ] Low memory (%u bytes contiguous) - preserving TLS keep-alive to avoid reconnect churn", maxAlloc);
        lowMemoryKeepAliveWarned = true;
    }

    const bool useKeepAlive = true;
    
    // Ensure WiFi is in high performance mode for upload
    WiFi.setSleep(false);
    
    // Upload via multipart POST using the same byte count that was hashed
    String path = "/api/v1/imports/" + currentImportId + "/files";
    String responseBody;
    int httpCode;
    
    String calculatedFileChecksum;
    if (!httpMultipartUpload(path, fileName, localPath, contentHash, lockedFileSize, sd, bytesTransferred, responseBody, httpCode, &calculatedFileChecksum, useKeepAlive)) {
        LOG_ERRORF("[SleepHQ] Upload failed for: %s", localPath.c_str());
        return false;
    }
    
    if (httpCode != 201 && httpCode != 200) {
        LOG_ERRORF("[SleepHQ] Upload returned HTTP %d for: %s", httpCode, localPath.c_str());
        LOG_ERRORF("[SleepHQ] Response: %s", responseBody.c_str());
        return false;
    }
    
    if (fileChecksum.length() == 0 && !calculatedFileChecksum.isEmpty()) {
        fileChecksum = calculatedFileChecksum;
    }
    
    LOG_DEBUGF("[SleepHQ] Uploaded: %s (%lu bytes)", fileName.c_str(), bytesTransferred);
    TrafficMonitor::getInstance().addTxBytes(bytesTransferred);
    TrafficMonitor::getInstance().addFile();
    return true;
}

void SleepHQUploader::end() {
    if (!currentImportId.isEmpty()) {
        processImport();
    }
    connected = false;
    accessToken = "";
    currentImportId = "";
}

bool SleepHQUploader::isConnected() const {
    return connected && !accessToken.isEmpty();
}

bool SleepHQUploader::isTlsAlive() const {
    return tlsClient && tlsClient->connected();
}

const String& SleepHQUploader::getTeamId() const { return teamId; }
const String& SleepHQUploader::getCurrentImportId() const { return currentImportId; }

unsigned long SleepHQUploader::getTokenRemainingSeconds() const {
    if (accessToken.isEmpty() || tokenExpiresIn == 0) return 0;
    unsigned long elapsed = (millis() - tokenObtainedAt) / 1000;
    if (elapsed >= tokenExpiresIn) return 0;
    return tokenExpiresIn - elapsed;
}

// ============================================================================
// HTTP Helpers
// ============================================================================

bool SleepHQUploader::httpRequest(const String& method, const String& path,
                                   const String& body, const String& contentType,
                                   String& responseBody, int& httpCode) {
    if (!tlsClient) {
        setupTLS();
    }
    
    if (!tlsClient) {
        LOG_ERROR("[SleepHQ] TLS client not available (OOM)");
        return false;
    }
    
    String baseUrl = config->getCloudBaseUrl();
    String url = baseUrl + path;
    
    // Retry loop: if the keep-alive connection was closed by the server, reconnect once
    for (int attempt = 0; attempt < 2; attempt++) {
        // Ensure we have a valid TLS client before attempting
        if (!tlsClient) {
            LOG_ERROR("[SleepHQ] TLS client lost during retry");
            return false;
        }

        // Check if heap has enough contiguous memory for SSL handshake.
        // Empirically, requests can still succeed a bit below 40KB on some builds,
        // so keep a safety floor without over-blocking recoverable sessions.
        uint32_t maxAlloc = ESP.getMaxAllocHeap();
        if (!tlsClient->connected() && maxAlloc < 36000) {
            LOG_ERRORF("[SleepHQ] Insufficient contiguous heap for SSL (%u bytes), skipping request", maxAlloc);
            return false;
        }

        HTTPClient http;
        http.begin(*tlsClient, url);
        http.setTimeout(15000);  // 15 second timeout
        http.setReuse(true);     // Keep-alive: reuse TLS connection across requests
        
        // Set headers
        http.addHeader("Accept", "application/vnd.api+json");
        if (!accessToken.isEmpty()) {
            http.addHeader("Authorization", "Bearer " + accessToken);
        }
        if (!contentType.isEmpty()) {
            http.addHeader("Content-Type", contentType);
        }
        
        // Feed watchdog before potentially long HTTP operation (TLS handshake + request)
        esp_task_wdt_reset();
        
        // Send request
        if (method == "GET") {
            httpCode = http.GET();
        } else if (method == "POST") {
            httpCode = body.isEmpty() ? http.POST("") : http.POST(body);
        } else {
            LOG_ERRORF("[SleepHQ] Unsupported HTTP method: %s", method.c_str());
            http.end();
            return false;
        }
        
        // Feed software watchdog heartbeat after HTTP operation completes
        extern volatile unsigned long g_uploadHeartbeat;
        g_uploadHeartbeat = millis();
        
        if (httpCode > 0) {
            responseBody = http.getString();
            http.end();
            return true;
        }
        
        // Connection error — retry once after reconnecting
        if (attempt == 0) {
            LOG_WARNF("[SleepHQ] HTTP %s failed (%s), waiting 3s before retry...",
                      method.c_str(), http.errorToString(httpCode).c_str());
            http.end();
            resetTLS(); // Full reset to clear FDs
            
            // Check WiFi status and cycle if needed to clear poisoned sockets
            if (WiFi.status() != WL_CONNECTED) {
                LOG_WARN("[SleepHQ] WiFi disconnected, waiting for reconnection...");
                unsigned long startWait = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - startWait < 10000) {
                    esp_task_wdt_reset();  // Feed watchdog during wait
                    delay(100);
                }
                if (WiFi.status() == WL_CONNECTED) {
                    LOG_INFO("[SleepHQ] WiFi reconnected");
                } else {
                    LOG_ERROR("[SleepHQ] WiFi still disconnected");
                }
            } else {
                // WiFi connected but request failed — attempt coordinated cycle.
                // Skips if SMB holds an active connection or cooldown has not elapsed.
                tryCoordinatedWifiCycle(true);
                resetTLS();
            }
            
            continue;
        }
        
        LOG_ERRORF("[SleepHQ] HTTP request failed after retry: %s", http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }
    return false;
}

bool SleepHQUploader::httpMultipartUpload(const String& path, const String& fileName,
                                           const String& filePath, const String& contentHash,
                                           unsigned long lockedFileSize,
                                           fs::FS &sd, unsigned long& bytesTransferred,
                                           String& responseBody, int& httpCode,
                                           String* calculatedChecksum,
                                           bool useKeepAlive) {
    if (!tlsClient) {
        setupTLS();
    }
    
    if (!tlsClient) {
        LOG_ERROR("[SleepHQ] TLS client not available (OOM)");
        return false;
    }
    
    // Open the file
    File file = sd.open(filePath, FILE_READ);
    if (!file) {
        LOG_ERRORF("[SleepHQ] Cannot open file: %s", filePath.c_str());
        return false;
    }
    // Use the locked file size (same byte count that was hashed) instead of
    // file.size() which may have grown since the hash was computed.
    unsigned long fileSize = lockedFileSize;
    
    // Extract the directory path for the 'path' field (stack-allocated to avoid heap churn)
    char dirPath[128];
    {
        const char* fp = filePath.c_str();
        const char* ls = strrchr(fp, '/');
        if (ls && ls > fp) {
            size_t dirLen = ls - fp;
            if (dirLen >= sizeof(dirPath)) dirLen = sizeof(dirPath) - 1;
            memcpy(dirPath, fp, dirLen);
            dirPath[dirLen] = '\0';
        } else {
            strcpy(dirPath, "/");
        }
    }
    
    // Build multipart boundary on stack (no heap allocation)
    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----ESP32Boundary%lu", millis());
    
    // Calculate exact part lengths using snprintf(NULL,0,...) — zero heap allocation
    const char* fnStr = fileName.c_str();
    int head1Len = snprintf(NULL, 0, "--%s\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n%s\r\n",
                            boundary, fnStr);
    int head2Len = snprintf(NULL, 0, "--%s\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n%s\r\n",
                            boundary, dirPath);
    int head3Len = snprintf(NULL, 0, "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                            "Content-Type: application/octet-stream\r\n\r\n",
                            boundary, fnStr);
    int foot1Len = snprintf(NULL, 0, "\r\n--%s\r\nContent-Disposition: form-data; name=\"content_hash\"\r\n\r\n",
                            boundary);
    int foot2Len = snprintf(NULL, 0, "\r\n--%s--\r\n", boundary);
    
    // Calculate total content length
    unsigned long totalLength = head1Len + head2Len + head3Len + 
                                fileSize + 
                                foot1Len + 32 + foot2Len;
    
    // Always stream multipart payloads.
    // This avoids large per-file temporary allocations (and HTTPClient internal allocations)
    // that can fragment heap and trigger TLS allocation failures later in the session.
    file.close();
    
    // Parse host and port from base URL — stack buffer to avoid heap churn
    char host[128];
    int port = 443;
    {
        const char* rawUrl = config->getCloudBaseUrl().c_str();
        const char* hostStart = rawUrl;
        if (strncmp(hostStart, "https://", 8) == 0) hostStart += 8;
        else if (strncmp(hostStart, "http://", 7) == 0) hostStart += 7;
        const char* hostEnd = strchr(hostStart, '/');
        size_t hostLen = hostEnd ? (size_t)(hostEnd - hostStart) : strlen(hostStart);
        if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
        memcpy(host, hostStart, hostLen);
        host[hostLen] = '\0';
        char* colonPos = strchr(host, ':');
        if (colonPos) {
            port = atoi(colonPos + 1);
            *colonPos = '\0';
        }
    }
    
    // Retry loop for streaming upload (same pattern as in-memory path)
    for (int attempt = 0; attempt < 2; attempt++) {
        // Ensure we have a valid TLS client before attempting
        if (!tlsClient) {
            LOG_ERROR("[SleepHQ] TLS client lost during retry");
            return false;
        }

        // Reuse existing TLS connection if still alive (keep-alive from previous request)
        if (!tlsClient->connected()) {
            LOGF("[SleepHQ] Streaming: establishing TLS connection (attempt %d, free: %u, max_alloc: %u)",
                 attempt + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            if (!tlsClient->connect(host, port)) {
                LOG_ERROR("[SleepHQ] Failed to connect for streaming upload");
                
                if (attempt == 0) {
                    resetTLS(); // Full reset instead of just stop()
                    
                    // Check WiFi status and wait if disconnected
                    if (WiFi.status() != WL_CONNECTED) {
                        LOG_WARN("[SleepHQ] WiFi disconnected during connect, waiting for reconnection...");
                        unsigned long startWait = millis();
                        while (WiFi.status() != WL_CONNECTED && millis() - startWait < 10000) {
                            extern volatile unsigned long g_uploadHeartbeat;
                            g_uploadHeartbeat = millis();  // Feed software watchdog
                            delay(100);
                        }
                        if (WiFi.status() == WL_CONNECTED) {
                            LOG_INFO("[SleepHQ] WiFi reconnected");
                        } else {
                            LOG_ERROR("[SleepHQ] WiFi still disconnected");
                        }
                    } else {
                        // WiFi connected but TLS connect failed — attempt coordinated cycle.
                        // Skips if SMB holds an active connection or cooldown has not elapsed.
                        // Only reset TLS again if the cycle actually ran (WiFi was cycled);
                        // if skipped, the fresh client from the resetTLS() above is still good.
                        if (tryCoordinatedWifiCycle(true)) {
                            resetTLS();
                            if (!tlsClient) {
                                LOG_ERROR("[SleepHQ] TLS client allocation failed after WiFi cycle (OOM)");
                                return false;
                            }
                        }
                    }
                    
                    continue; 
                }
                return false;
            }
        } else {
            LOG_DEBUG("[SleepHQ] Streaming: reusing existing TLS connection (keep-alive)");
        }
        
        // Send HTTP POST request headers — use stack buffer to avoid String heap churn
        {
            char hdrBuf[256];
            int n;
            n = snprintf(hdrBuf, sizeof(hdrBuf), "POST %s HTTP/1.1\r\n", path.c_str());
            tlsClient->write((const uint8_t*)hdrBuf, n);
            n = snprintf(hdrBuf, sizeof(hdrBuf), "Host: %s\r\n", host);
            tlsClient->write((const uint8_t*)hdrBuf, n);
            n = snprintf(hdrBuf, sizeof(hdrBuf), "Authorization: Bearer %s\r\n", accessToken.c_str());
            tlsClient->write((const uint8_t*)hdrBuf, n);
            n = snprintf(hdrBuf, sizeof(hdrBuf), "Accept: application/vnd.api+json\r\n");
            tlsClient->write((const uint8_t*)hdrBuf, n);
            n = snprintf(hdrBuf, sizeof(hdrBuf), "Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
            tlsClient->write((const uint8_t*)hdrBuf, n);
            n = snprintf(hdrBuf, sizeof(hdrBuf), "Content-Length: %lu\r\n", totalLength);
            tlsClient->write((const uint8_t*)hdrBuf, n);
            if (useKeepAlive) {
                tlsClient->write((const uint8_t*)"Connection: keep-alive\r\n\r\n", 26);
            } else {
                tlsClient->write((const uint8_t*)"Connection: close\r\n\r\n", 21);
            }
        }
        
        // Send multipart preamble — stack buffer to avoid heap churn
        {
            char partBuf[384];
            int n;
            n = snprintf(partBuf, sizeof(partBuf), "--%s\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n%s\r\n",
                         boundary, fnStr);
            tlsClient->write((const uint8_t*)partBuf, n);
            n = snprintf(partBuf, sizeof(partBuf), "--%s\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n%s\r\n",
                         boundary, dirPath);
            tlsClient->write((const uint8_t*)partBuf, n);
            n = snprintf(partBuf, sizeof(partBuf), "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                         "Content-Type: application/octet-stream\r\n\r\n",
                         boundary, fnStr);
            tlsClient->write((const uint8_t*)partBuf, n);
        }
        
        // Re-open and stream file
        file = sd.open(filePath, FILE_READ);
        if (!file) {
            LOG_ERROR("[SleepHQ] Cannot re-open file for streaming");
            return false;
        }
        
        struct MD5Context md5ctx;
        MD5Init(&md5ctx);
        
        uint8_t buffer[CLOUD_UPLOAD_BUFFER_SIZE];
        unsigned long totalSent = 0;
        bool writeError = false;
        int readRetries = 0;
        
        while (totalSent < fileSize) {
            size_t toRead = sizeof(buffer);
            if (fileSize - totalSent < toRead) {
                toRead = fileSize - totalSent;
            }
            
            size_t bytesRead = file.read(buffer, toRead);
            if (bytesRead == 0) {
                // Unexpected EOF or read error - try to recover
                if (readRetries < 3) {
                    LOG_WARNF("[SleepHQ] Read returned 0 at %lu/%lu, retrying (%d/3)...", totalSent, fileSize, readRetries + 1);
                    delay(100);
                    readRetries++;
                    continue;
                }
                LOG_ERRORF("[SleepHQ] File read failed at %lu/%lu bytes", totalSent, fileSize);
                break;
            }
            readRetries = 0; // Reset retry counter on success
            
            // Update checksum with file data
            MD5Update(&md5ctx, buffer, bytesRead);
            
            // Write to TLS with retry and partial write handling
            size_t remainingToWrite = bytesRead;
            uint8_t* writePtr = buffer;
            int writeRetries = 0;
            
            while (remainingToWrite > 0) {
                size_t written = tlsClient->write(writePtr, remainingToWrite);
                
                if (written > 0) {
                    remainingToWrite -= written;
                    writePtr += written;
                    totalSent += written;
                    writeRetries = 0; // Reset retry counter on success
                } else {
                    // Write failed or returned 0 (buffer full / EAGAIN)
                    if (!tlsClient->connected()) {
                        LOG_ERROR("[SleepHQ] Connection lost during write");
                        writeError = true;
                        break;
                    }
                    
                    if (writeRetries < 10) {
                        LOG_WARNF("[SleepHQ] Write returned 0/fail at %lu/%lu, retrying (%d/10)...", totalSent, fileSize, writeRetries + 1);
                        delay(500); // Wait longer for socket buffer to drain
                        writeRetries++;
                        yield();
                        continue;
                    } else {
                        LOG_ERRORF("[SleepHQ] Write timeout/fail after 10 retries at %lu/%lu", totalSent, fileSize);
                        writeError = true;
                        break;
                    }
                }
            }
            
            if (writeError) {
                break;
            }
            
            // Feed software watchdog during large file streaming
            extern volatile unsigned long g_uploadHeartbeat;
            g_uploadHeartbeat = millis();
        }
        file.close();

        if (totalSent != fileSize) {
            if (writeError) {
                LOG_WARNF("[SleepHQ] Upload interrupted by write error (%lu/%lu bytes), reconnecting...", totalSent, fileSize);
            } else {
                LOG_WARNF("[SleepHQ] Short read from file (%lu/%lu bytes), reconnecting...", totalSent, fileSize);
            }
            resetTLS(); // Full reset to clear FDs
            
            // resetTLS() above already created a fresh WiFiClientSecure.
            // Attempt a coordinated cycle only if SMB is idle and cooldown allows;
            // write errors are usually transient buffer pressure, not dead WiFi.
            tryCoordinatedWifiCycle(true);
            
            if (attempt == 0) {
                continue;
            }
            return false;
        }
        
        if (writeError) {
            resetTLS(); // Full reset to clear FDs
            
            // Attempt a coordinated cycle only if SMB is idle and cooldown allows.
            tryCoordinatedWifiCycle(true);
            
            if (attempt == 0) {
                LOG_WARN("[SleepHQ] Streaming write failed, reconnecting...");
                continue;
            }
            return false;
        }
        
        // Finalize checksum — use stack buffer, no String allocation
        uint8_t digest[16];
        MD5Final(digest, &md5ctx);
        char hashStr[33];
        for (int i = 0; i < 16; i++) {
            sprintf(hashStr + (i * 2), "%02x", digest[i]);
        }
        hashStr[32] = '\0';
        
        if (calculatedChecksum) {
            *calculatedChecksum = String(hashStr);
        }
        
        // Send footer with hash — stack buffer to avoid heap churn
        {
            char partBuf[384];
            int n;
            n = snprintf(partBuf, sizeof(partBuf), "\r\n--%s\r\nContent-Disposition: form-data; name=\"content_hash\"\r\n\r\n",
                         boundary);
            tlsClient->write((const uint8_t*)partBuf, n);
            tlsClient->write((const uint8_t*)hashStr, 32);
            n = snprintf(partBuf, sizeof(partBuf), "\r\n--%s--\r\n", boundary);
            tlsClient->write((const uint8_t*)partBuf, n);
        }
        tlsClient->flush();
        
        // Read response status line
        unsigned long timeout = millis() + 30000;
        while (!tlsClient->available() && millis() < timeout) {
            delay(10);
        }

        if (!tlsClient->available()) {
            LOG_WARN("[SleepHQ] Streaming response timeout, reconnecting...");
            resetTLS(); // Full reset to clear FDs
            
            // Check WiFi status
            if (WiFi.status() != WL_CONNECTED) {
                LOG_WARN("[SleepHQ] WiFi disconnected awaiting response, waiting for reconnection...");
                unsigned long startWait = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - startWait < 10000) {
                    extern volatile unsigned long g_uploadHeartbeat;
                    g_uploadHeartbeat = millis();
                    delay(100);
                }
            }
            
            if (attempt == 0) {
                continue;
            }
            return false;
        }
        
        // Parse status line — stack buffer, no heap alloc
        char lineBuf[256];
        int lineLen = 0;
        {
            unsigned long ld = millis() + 5000;
            while (millis() < ld) {
                if (!tlsClient->available()) { delay(2); continue; }
                int c = tlsClient->read();
                if (c < 0 || c == '\n') break;
                if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
            }
            lineBuf[lineLen] = '\0';
        }
        {
            const char* sp = strchr(lineBuf, ' ');
            httpCode = sp ? atoi(sp + 1) : -1;
        }
        
        // Parse response headers — stack buffer, no heap alloc
        long contentLength = -1;
        bool isChunked = false;
        bool connectionClose = false;
        bool headersDone = false;
        unsigned long headerDeadline = millis() + 5000;
        while (millis() < headerDeadline) {
            if (!tlsClient->available()) {
                delay(2);
                continue;
            }
            lineLen = 0;
            while (millis() < headerDeadline) {
                if (!tlsClient->available()) { delay(2); continue; }
                int c = tlsClient->read();
                if (c < 0 || c == '\n') break;
                if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
            }
            lineBuf[lineLen] = '\0';
            if (lineLen == 0) {
                headersDone = true;
                break;
            }
            if (strncasecmp(lineBuf, "Content-Length:", 15) == 0) {
                contentLength = atol(lineBuf + 15);
            } else if (strncasecmp(lineBuf, "Transfer-Encoding:", 18) == 0) {
                for (int ci = 18; lineBuf[ci]; ci++) lineBuf[ci] = tolower((unsigned char)lineBuf[ci]);
                if (strstr(lineBuf + 18, "chunked")) isChunked = true;
            } else if (strncasecmp(lineBuf, "Connection:", 11) == 0) {
                for (int ci = 11; lineBuf[ci]; ci++) lineBuf[ci] = tolower((unsigned char)lineBuf[ci]);
                if (strstr(lineBuf + 11, "close")) connectionClose = true;
            }
        }

        if (!headersDone) {
            LOG_WARN("[SleepHQ] Incomplete response headers, reconnecting...");
            resetTLS(); // Full reset to clear FDs
            
            // Check WiFi status
            if (WiFi.status() != WL_CONNECTED) {
                LOG_WARN("[SleepHQ] WiFi disconnected during headers, waiting for reconnection...");
                unsigned long startWait = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - startWait < 10000) {
                    extern volatile unsigned long g_uploadHeartbeat;
                    g_uploadHeartbeat = millis();
                    delay(100);
                }
            }
            
            if (attempt == 0) {
                continue;
            }
            return false;
        }
        
        // Drain response body — stack buffer to avoid heap fragmentation
        // (responseBody String built char-by-char was the #1 fragmentation source)
        char respBuf[1024];
        int respLen = 0;
        if (isChunked) {
            unsigned long chunkDeadline = millis() + 5000;
            while (millis() < chunkDeadline) {
                if (!tlsClient->available()) {
                    delay(2);
                    continue;
                }
                // Read chunk size line — stack buffer
                lineLen = 0;
                while (millis() < chunkDeadline) {
                    if (!tlsClient->available()) { delay(2); continue; }
                    int c = tlsClient->read();
                    if (c < 0 || c == '\n') break;
                    if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                }
                lineBuf[lineLen] = '\0';
                long chunkSize = strtol(lineBuf, NULL, 16);
                
                if (chunkSize <= 0) {
                    // End chunk — drain trailers using stack buffer
                    unsigned long trailerDl = millis() + 2000;
                    while (tlsClient->available() && millis() < trailerDl) {
                        lineLen = 0;
                        while (millis() < trailerDl) {
                            if (!tlsClient->available()) { delay(2); continue; }
                            int c = tlsClient->read();
                            if (c < 0 || c == '\n') break;
                            if (c != '\r' && lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                        }
                        if (lineLen == 0) break;
                    }
                    break;
                }
                
                // Read chunk data
                long remaining = chunkSize;
                while (remaining > 0 && millis() < chunkDeadline) {
                    if (!tlsClient->available()) { delay(2); continue; }
                    uint8_t drainBuf[256];
                    size_t toRead = (remaining < (long)sizeof(drainBuf)) ? remaining : sizeof(drainBuf);
                    size_t r = tlsClient->read(drainBuf, toRead);
                    if (r == 0) break;
                    for (size_t i = 0; i < r && respLen < (int)sizeof(respBuf) - 1; i++) {
                        respBuf[respLen++] = (char)drainBuf[i];
                    }
                    remaining -= r;
                    chunkDeadline = millis() + 5000;
                }
                
                // Read trailing CRLF — byte-by-byte, no heap alloc
                while (tlsClient->available()) {
                    int c = tlsClient->read();
                    if (c == '\n' || c < 0) break;
                }
            }
        } else if (contentLength > 0) {
            long remaining = contentLength;
            unsigned long bodyDeadline = millis() + 5000;
            while (remaining > 0 && millis() < bodyDeadline) {
                if (!tlsClient->available()) {
                    delay(2);
                    continue;
                }
                uint8_t drainBuf[256];
                size_t toRead = (remaining < (long)sizeof(drainBuf)) ? remaining : sizeof(drainBuf);
                size_t r = tlsClient->read(drainBuf, toRead);
                if (r == 0) break;
                for (size_t i = 0; i < r && respLen < (int)sizeof(respBuf) - 1; i++) {
                    respBuf[respLen++] = (char)drainBuf[i];
                }
                remaining -= r;
                bodyDeadline = millis() + 5000;
            }
            if (remaining > 0) {
                LOG_WARNF("[SleepHQ] Response body not fully drained (%ld bytes left), resetting TLS", remaining);
                tlsClient->stop();
            }
        } else {
            // Unknown body length: drain briefly
            unsigned long drainDeadline = millis() + 300;
            while (millis() < drainDeadline) {
                while (tlsClient->available()) {
                    int c = tlsClient->read();
                    if (c >= 0 && respLen < (int)sizeof(respBuf) - 1) {
                        respBuf[respLen++] = (char)c;
                    }
                    drainDeadline = millis() + 300;
                }
                delay(2);
            }
            tlsClient->stop();
        }
        
        // Single heap allocation from stack buffer (only for error logging)
        respBuf[respLen] = '\0';
        responseBody = respBuf;
        
        // Keep TLS alive unless server requested close
        if (connectionClose) {
            tlsClient->stop();
        }

        bytesTransferred = totalSent;
        
        // Feed software watchdog after successful file upload
        {
            extern volatile unsigned long g_uploadHeartbeat;
            g_uploadHeartbeat = millis();
        }
        
        return httpCode > 0;
    }
    return false;
}

#endif // ENABLE_SLEEPHQ_UPLOAD
