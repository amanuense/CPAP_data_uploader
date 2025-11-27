#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>

Config::Config() : 
    uploadHour(12),  // Default: noon
    sessionDurationSeconds(5),  // Default: 5 seconds
    maxRetryAttempts(3),  // Default: 3 attempts
    gmtOffsetSeconds(0),  // Default: UTC
    daylightOffsetSeconds(0),  // Default: no DST
    isValid(false)
{}

bool Config::loadFromSD(fs::FS &sd) {
    File configFile = sd.open("/config.json");
    if (!configFile) {
        LOG("Failed to open config file");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        LOGF("Failed to parse config: %s", error.c_str());
        return false;
    }

    wifiSSID = doc["WIFI_SSID"] | "";
    wifiPassword = doc["WIFI_PASS"] | "";
    schedule = doc["SCHEDULE"] | "";
    endpoint = doc["ENDPOINT"] | "";
    endpointType = doc["ENDPOINT_TYPE"] | "";
    endpointUser = doc["ENDPOINT_USER"] | "";
    endpointPassword = doc["ENDPOINT_PASS"] | "";
    
    // Parse new configuration fields with defaults
    uploadHour = doc["UPLOAD_HOUR"] | 12;
    sessionDurationSeconds = doc["SESSION_DURATION_SECONDS"] | 5;
    maxRetryAttempts = doc["MAX_RETRY_ATTEMPTS"] | 3;
    gmtOffsetSeconds = doc["GMT_OFFSET_SECONDS"] | 0L;
    daylightOffsetSeconds = doc["DAYLIGHT_OFFSET_SECONDS"] | 0;

    isValid = !wifiSSID.isEmpty() && !endpoint.isEmpty();
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
long Config::getGmtOffsetSeconds() const { return gmtOffsetSeconds; }
int Config::getDaylightOffsetSeconds() const { return daylightOffsetSeconds; }
bool Config::valid() const { return isValid; }
