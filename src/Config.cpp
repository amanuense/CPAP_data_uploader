#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>

// Define static constants for Preferences
const char* Config::PREFS_NAMESPACE = "cpap_creds";
const char* Config::PREFS_KEY_WIFI_PASS = "wifi_pass";
const char* Config::PREFS_KEY_ENDPOINT_PASS = "endpoint_pass";
const char* Config::CENSORED_VALUE = "***STORED_IN_FLASH***";

Config::Config() : 
    uploadHour(12),  // Default: noon
    sessionDurationSeconds(5),  // Default: 5 seconds
    maxRetryAttempts(3),  // Default: 3 attempts
    gmtOffsetHours(0),  // Default: UTC
    isValid(false),
    storePlainText(false),  // Default: secure mode
    credentialsInFlash(false)  // Will be set during loadFromSD
{}

Config::~Config() {
    closePreferences();
}

bool Config::initPreferences() {
    // Attempt to open Preferences namespace in read-write mode
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        LOG("ERROR: Failed to initialize Preferences namespace");
        LOG("Falling back to plain text credential storage");
        // Fall back to plain text mode on failure
        storePlainText = true;
        credentialsInFlash = false;
        return false;
    }
    
    LOG("Preferences initialized successfully");
    LOGF("Using Preferences namespace: %s", PREFS_NAMESPACE);
    return true;
}

void Config::closePreferences() {
    // Close Preferences to free resources
    preferences.end();
    LOG("Preferences closed");
}

bool Config::storeCredential(const char* key, const String& value) {
    // Validate that the credential is not empty
    if (value.isEmpty()) {
        LOGF("WARNING: Attempted to store empty credential for key '%s'", key);
        return false;
    }
    
    // Attempt to write the credential to Preferences
    size_t written = preferences.putString(key, value);
    
    if (written == 0) {
        LOGF("ERROR: Failed to store credential '%s' in Preferences", key);
        return false;
    }
    
    LOGF("Credential '%s' stored successfully in Preferences (%d bytes)", key, written);
    return true;
}

String Config::loadCredential(const char* key, const String& defaultValue) {
    // Attempt to read the credential from Preferences
    String value = preferences.getString(key, defaultValue);
    
    // Check if we got the default value (key not found)
    if (value == defaultValue) {
        LOGF("WARNING: Credential '%s' not found in Preferences, using default", key);
    } else {
        // Validate that the retrieved credential is not empty
        if (value.isEmpty()) {
            LOGF("WARNING: Credential '%s' retrieved from Preferences is empty, using default", key);
            return defaultValue;
        }
        LOGF("Credential '%s' loaded successfully from Preferences", key);
    }
    
    return value;
}

bool Config::isCensored(const String& value) {
    // Check if the value matches the censored placeholder
    return value.equals(CENSORED_VALUE);
}

bool Config::censorConfigFile(fs::FS &sd) {
    LOG("Starting config file censoring operation");
    
    // Read existing config.json
    File configFile = sd.open("/config.json", FILE_READ);
    if (!configFile) {
        LOG("ERROR: Cannot open config.json for reading during censoring");
        return false;
    }
    
    // Parse JSON document
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        LOGF("ERROR: Failed to parse config.json for censoring: %s", error.c_str());
        return false;
    }
    
    // Update credential fields with censored values
    doc["WIFI_PASS"] = CENSORED_VALUE;
    doc["ENDPOINT_PASS"] = CENSORED_VALUE;
    
    LOG("Credential fields updated with censored values");
    
    // Write updated JSON back to SD card
    configFile = sd.open("/config.json", FILE_WRITE);
    if (!configFile) {
        LOG("ERROR: Cannot open config.json for writing during censoring");
        return false;
    }
    
    // Serialize JSON to file
    size_t bytesWritten = serializeJson(doc, configFile);
    configFile.close();
    
    if (bytesWritten == 0) {
        LOG("ERROR: Failed to write censored config to file");
        return false;
    }
    
    LOGF("Config file censored successfully (%d bytes written)", bytesWritten);
    LOG("Credentials are now stored securely in flash memory");
    
    return true;
}

bool Config::migrateToSecureStorage(fs::FS &sd) {
    LOG("========================================");
    LOG("Starting credential migration to secure storage");
    LOG("========================================");
    
    // Step 1: Validate that credentials are not empty
    if (wifiPassword.isEmpty() && endpointPassword.isEmpty()) {
        LOG("WARNING: Both credentials are empty, skipping migration");
        return false;
    }
    
    if (wifiPassword.isEmpty()) {
        LOG("WARNING: WiFi password is empty, will not migrate WIFI_PASS");
    }
    
    if (endpointPassword.isEmpty()) {
        LOG("WARNING: Endpoint password is empty, will not migrate ENDPOINT_PASS");
    }
    
    // Step 2: Store WIFI_PASS in Preferences
    bool wifiStored = false;
    if (!wifiPassword.isEmpty()) {
        LOG("Storing WiFi password in Preferences...");
        wifiStored = storeCredential(PREFS_KEY_WIFI_PASS, wifiPassword);
        
        if (!wifiStored) {
            LOG("ERROR: Failed to store WiFi password in Preferences");
            LOG("Migration aborted - keeping plain text credentials");
            return false;
        }
    } else {
        wifiStored = true;  // Skip if empty
    }
    
    // Step 3: Store ENDPOINT_PASS in Preferences
    bool endpointStored = false;
    if (!endpointPassword.isEmpty()) {
        LOG("Storing endpoint password in Preferences...");
        endpointStored = storeCredential(PREFS_KEY_ENDPOINT_PASS, endpointPassword);
        
        if (!endpointStored) {
            LOG("ERROR: Failed to store endpoint password in Preferences");
            LOG("Migration aborted - keeping plain text credentials");
            return false;
        }
    } else {
        endpointStored = true;  // Skip if empty
    }
    
    // Step 4: Verify credentials were stored correctly by reading back
    LOG("Verifying stored credentials...");
    bool verificationPassed = true;
    
    if (!wifiPassword.isEmpty()) {
        String verifyWifi = loadCredential(PREFS_KEY_WIFI_PASS, "");
        if (verifyWifi != wifiPassword) {
            LOG("ERROR: WiFi password verification failed - stored value does not match");
            verificationPassed = false;
        } else {
            LOG("WiFi password verification: PASSED");
        }
    }
    
    if (!endpointPassword.isEmpty()) {
        String verifyEndpoint = loadCredential(PREFS_KEY_ENDPOINT_PASS, "");
        if (verifyEndpoint != endpointPassword) {
            LOG("ERROR: Endpoint password verification failed - stored value does not match");
            verificationPassed = false;
        } else {
            LOG("Endpoint password verification: PASSED");
        }
    }
    
    if (!verificationPassed) {
        LOG("ERROR: Credential verification failed");
        LOG("Migration aborted - keeping plain text credentials");
        return false;
    }
    
    LOG("All credentials verified successfully");
    
    // Step 5: Censor config.json after successful Preferences storage
    LOG("Censoring config.json file...");
    if (!censorConfigFile(sd)) {
        LOG("ERROR: Failed to censor config.json");
        LOG("WARNING: Credentials are stored in Preferences but config.json still contains plain text");
        LOG("Manual intervention may be required");
        return false;
    }
    
    // Step 6: Log success
    LOG("========================================");
    LOG("Credential migration completed successfully");
    LOG("Credentials are now stored securely in flash memory");
    LOG("config.json has been updated with censored values");
    LOG("========================================");
    
    return true;
}

bool Config::loadFromSD(fs::FS &sd) {
    LOG("========================================");
    LOG("Loading configuration from SD card");
    LOG("========================================");
    
    // Step 1: Read and parse config.json
    File configFile = sd.open("/config.json");
    if (!configFile) {
        LOG("ERROR: Failed to open config file");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        LOGF("ERROR: Failed to parse config: %s", error.c_str());
        return false;
    }

    LOG("Config file parsed successfully");
    
    // Step 2: Parse STORE_CREDENTIALS_PLAIN_TEXT flag (defaults to false for secure mode)
    storePlainText = doc["STORE_CREDENTIALS_PLAIN_TEXT"] | false;
    
    if (storePlainText) {
        LOG("========================================");
        LOG("PLAIN TEXT MODE: Credentials will be stored in config.json");
        LOG("========================================");
    } else {
        LOG("========================================");
        LOG("SECURE MODE: Credentials will be stored in flash memory");
        LOG("========================================");
    }
    
    // Step 3: Load non-credential configuration fields
    wifiSSID = doc["WIFI_SSID"] | "";
    schedule = doc["SCHEDULE"] | "";
    endpoint = doc["ENDPOINT"] | "";
    endpointType = doc["ENDPOINT_TYPE"] | "";
    endpointUser = doc["ENDPOINT_USER"] | "";
    
    // Parse new configuration fields with defaults
    uploadHour = doc["UPLOAD_HOUR"] | 12;
    sessionDurationSeconds = doc["SESSION_DURATION_SECONDS"] | 5;
    maxRetryAttempts = doc["MAX_RETRY_ATTEMPTS"] | 3;
    gmtOffsetHours = doc["GMT_OFFSET_HOURS"] | 0;
    
    // Step 4: Load credentials based on storage mode
    if (storePlainText) {
        // Plain text mode: Load credentials directly from config.json
        LOG("Loading credentials from config.json (plain text mode)");
        wifiPassword = doc["WIFI_PASS"] | "";
        endpointPassword = doc["ENDPOINT_PASS"] | "";
        credentialsInFlash = false;
        
        LOG("Credentials loaded from config.json");
        LOG("WARNING: Credentials are stored in plain text");
    } else {
        // Secure mode: Check if credentials are censored and handle accordingly
        LOG("Checking credential storage status...");
        
        // Initialize Preferences for secure storage
        if (!initPreferences()) {
            LOG("ERROR: Failed to initialize Preferences");
            LOG("Falling back to plain text mode for this session");
            wifiPassword = doc["WIFI_PASS"] | "";
            endpointPassword = doc["ENDPOINT_PASS"] | "";
            credentialsInFlash = false;
            storePlainText = true;  // Force plain text mode due to Preferences failure
        } else {
            // Read credential values from config.json
            String configWifiPass = doc["WIFI_PASS"] | "";
            String configEndpointPass = doc["ENDPOINT_PASS"] | "";
            
            // Check if credentials are censored
            bool wifiCensored = isCensored(configWifiPass);
            bool endpointCensored = isCensored(configEndpointPass);
            
            if (wifiCensored && endpointCensored) {
                // Both credentials are censored - load from Preferences
                LOG("Credentials are censored in config.json");
                LOG("Loading credentials from flash memory (Preferences)...");
                
                wifiPassword = loadCredential(PREFS_KEY_WIFI_PASS, "");
                endpointPassword = loadCredential(PREFS_KEY_ENDPOINT_PASS, "");
                credentialsInFlash = true;
                
                LOG("Credentials loaded from flash memory");
                LOG("Credential storage: SECURE (flash memory)");
                
            } else if (!wifiCensored && !endpointCensored) {
                // Credentials are NOT censored - trigger migration
                LOG("Credentials are in plain text in config.json");
                LOG("Migration to secure storage required");
                
                // Temporarily store plain text credentials
                wifiPassword = configWifiPass;
                endpointPassword = configEndpointPass;
                
                // Attempt migration
                if (migrateToSecureStorage(sd)) {
                    LOG("Migration successful - credentials now in flash memory");
                    credentialsInFlash = true;
                } else {
                    LOG("ERROR: Migration failed - continuing with plain text credentials");
                    LOG("WARNING: Credentials remain in plain text in config.json");
                    credentialsInFlash = false;
                }
                
            } else {
                // Mixed state - some censored, some not (unexpected)
                LOG("WARNING: Mixed credential state detected");
                LOGF("WiFi password censored: %s", wifiCensored ? "YES" : "NO");
                LOGF("Endpoint password censored: %s", endpointCensored ? "YES" : "NO");
                LOG("Loading available credentials from both sources");
                
                // Load from appropriate source for each credential
                if (wifiCensored) {
                    wifiPassword = loadCredential(PREFS_KEY_WIFI_PASS, configWifiPass);
                } else {
                    wifiPassword = configWifiPass;
                }
                
                if (endpointCensored) {
                    endpointPassword = loadCredential(PREFS_KEY_ENDPOINT_PASS, configEndpointPass);
                } else {
                    endpointPassword = configEndpointPass;
                }
                
                credentialsInFlash = (wifiCensored || endpointCensored);
                LOG("WARNING: Consider re-migrating to ensure consistent credential storage");
            }
        }
    }
    
    // Step 5: Validate configuration
    isValid = !wifiSSID.isEmpty() && !endpoint.isEmpty();
    
    if (isValid) {
        LOG("========================================");
        LOG("Configuration loaded successfully");
        LOGF("Storage mode: %s", storePlainText ? "PLAIN TEXT" : "SECURE");
        LOGF("Credentials in flash: %s", credentialsInFlash ? "YES" : "NO");
        LOG("========================================");
    } else {
        LOG("ERROR: Configuration validation failed");
        LOG("Missing required fields: WIFI_SSID or ENDPOINT");
    }
    
    return isValid;
}

const String& Config::getWifiSSID() const { return wifiSSID; }
const String& Config::getWifiPassword() const { return wifiPassword; }
const String& Config::getSchedule() const { return schedule; }
const String& Config::getEndpoint() const { return endpoint; }
const String& Config::getEndpointType() const { return endpointType; }
const String& Config::getEndpointUser() const { return endpointUser; }
const String& Config::getEndpointPassword() const { return endpointPassword; }
int Config::getUploadHour() const { return uploadHour; }
int Config::getSessionDurationSeconds() const { return sessionDurationSeconds; }
int Config::getMaxRetryAttempts() const { return maxRetryAttempts; }
int Config::getGmtOffsetHours() const { return gmtOffsetHours; }
bool Config::valid() const { return isValid; }

// Credential storage mode getters
bool Config::isStoringPlainText() const { return storePlainText; }
bool Config::areCredentialsInFlash() const { return credentialsInFlash; }
