#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FS.h>

// Conditionally include Preferences for ESP32 or use mock for testing
#ifdef UNIT_TEST
    #include "MockPreferences.h"
#else
    #include <Preferences.h>
#endif

class Config {
private:
    String wifiSSID;
    String wifiPassword;
    String schedule;
    String endpoint;
    String endpointType;  // SMB, WEBDAV, SLEEPHQ
    String endpointUser;
    String endpointPassword;
    int uploadHour;
    int sessionDurationSeconds;
    int maxRetryAttempts;
    int gmtOffsetHours;
    bool isValid;
    
    // Credential storage mode flags
    bool storePlainText;
    bool credentialsInFlash;
    
    // Preferences object for secure credential storage
    Preferences preferences;
    
    // Preferences constants
    static const char* PREFS_NAMESPACE;
    static const char* PREFS_KEY_WIFI_PASS;
    static const char* PREFS_KEY_ENDPOINT_PASS;
    static const char* CENSORED_VALUE;
    
    // Preferences initialization and cleanup methods
    bool initPreferences();
    void closePreferences();
    
    // Credential storage and retrieval methods
    bool storeCredential(const char* key, const String& value);
    String loadCredential(const char* key, const String& defaultValue);
    bool isCensored(const String& value);
    
    // Config file censoring method
    bool censorConfigFile(fs::FS &sd);
    
    // Credential migration method
    bool migrateToSecureStorage(fs::FS &sd);

public:
    Config();
    ~Config();
    
    bool loadFromSD(fs::FS &sd);
    
    const String& getWifiSSID() const;
    const String& getWifiPassword() const;
    const String& getSchedule() const;
    const String& getEndpoint() const;
    const String& getEndpointType() const;
    const String& getEndpointUser() const;
    const String& getEndpointPassword() const;
    int getUploadHour() const;
    int getSessionDurationSeconds() const;
    int getMaxRetryAttempts() const;
    int getGmtOffsetHours() const;
    bool valid() const;
    
    // Credential storage mode getters
    bool isStoringPlainText() const;
    bool areCredentialsInFlash() const;
};

#endif // CONFIG_H
