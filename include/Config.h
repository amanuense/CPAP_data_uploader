#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FS.h>

// Power management enums
enum class WifiTxPower {
    POWER_HIGH,
    POWER_MID,
    POWER_LOW
};

enum class WifiPowerSaving {
    SAVE_NONE,
    SAVE_MID,
    SAVE_MAX
};

// Conditionally include Preferences for ESP32 or use mock for testing
#ifdef UNIT_TEST
    #include "MockPreferences.h"
#else
    #include <Preferences.h>
#endif

class Config {
public:
    // Max line length for config file
    static const size_t MAX_LINE_LENGTH = 256;

private:
    String wifiSSID;
    String wifiPassword;
    String hostname;  // mDNS hostname (defaults to "cpap")
    String schedule;
    String endpoint;
    String endpointType;  // SMB, CLOUD, SMB,CLOUD
    String endpointUser;
    String endpointPassword;
    int gmtOffsetHours;
    bool logToSdCard;
    bool debugMode;
    bool isValid;
    
    // Cloud upload settings
    String cloudClientId;
    String cloudClientSecret;
    String cloudTeamId;
    String cloudBaseUrl;
    int cloudDeviceId;
    int maxDays;
    int recentFolderDays;
    bool cloudInsecureTls;
    
    // Upload FSM settings
    String uploadMode;             // "scheduled" or "smart"
    int uploadStartHour;           // 0-23, start of upload window
    int uploadEndHour;             // 0-23, end of upload window
    int inactivitySeconds;         // Z: seconds of bus silence before upload
    int exclusiveAccessMinutes;    // X: max minutes of exclusive SD access
    int cooldownMinutes;           // Y: minutes to release SD between upload cycles

    // Webhook notification settings
    String sleepLabDomain;
    String sleepLabUserId;
    String genericWebhookUrl;
    
    // Cached endpoint type flags (computed once during loadFromSD)
    bool _hasSmbEndpoint;
    bool _hasCloudEndpoint;
    bool _hasWebdavEndpoint;
    
    // Power management settings
    int cpuSpeedMhz;
    WifiTxPower wifiTxPower;
    WifiPowerSaving wifiPowerSaving;
    
    // Credential storage mode flags
    bool storePlainText;
    bool credentialsInFlash;
    
    // Preferences object for secure credential storage
    Preferences preferences;
    
    // Preferences constants
    static const char* PREFS_NAMESPACE;
    static const char* PREFS_KEY_WIFI_PASS;
    static const char* PREFS_KEY_ENDPOINT_PASS;
    static const char* PREFS_KEY_CLOUD_SECRET;
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

    // Parsing helpers
    void parseLine(String& line);
    void setConfigValue(String key, String value);
    String trimComment(String line);

public:
    Config();
    ~Config();
    
    bool loadFromSD(fs::FS &sd);
    
    const String& getWifiSSID() const;
    const String& getWifiPassword() const;
    const String& getHostname() const;
    const String& getSchedule() const;
    const String& getEndpoint() const;
    const String& getEndpointType() const;
    const String& getEndpointUser() const;
    const String& getEndpointPassword() const;
    int getGmtOffsetHours() const;
    bool getLogToSdCard() const;
    bool getDebugMode() const;
    bool valid() const;
    
    // Cloud upload getters
    const String& getCloudClientId() const;
    const String& getCloudClientSecret() const;
    const String& getCloudTeamId() const;
    const String& getCloudBaseUrl() const;
    int getCloudDeviceId() const;
    int getMaxDays() const;
    int getRecentFolderDays() const;
    bool getCloudInsecureTls() const;
    bool hasCloudEndpoint() const;
    bool hasSmbEndpoint() const;
    bool hasWebdavEndpoint() const;
    
    // Upload FSM getters
    const String& getUploadMode() const;
    int getUploadStartHour() const;
    int getUploadEndHour() const;
    int getInactivitySeconds() const;
    int getExclusiveAccessMinutes() const;
    int getCooldownMinutes() const;
    bool isSmartMode() const;

    // Webhook notification getters
    const String& getSleepLabDomain() const;
    const String& getSleepLabUserId() const;
    const String& getGenericWebhookUrl() const;
    
    // Power management getters
    int getCpuSpeedMhz() const;
    WifiTxPower getWifiTxPower() const;
    WifiPowerSaving getWifiPowerSaving() const;
    
    // Credential storage mode getters
    bool isStoringPlainText() const;
    bool areCredentialsInFlash() const;
    
private:
    // Helper methods for enum conversion
    static WifiTxPower parseWifiTxPower(const String& str);
    static WifiPowerSaving parseWifiPowerSaving(const String& str);
    static String wifiTxPowerToString(WifiTxPower power);
    static String wifiPowerSavingToString(WifiPowerSaving saving);
};

#endif // CONFIG_H
