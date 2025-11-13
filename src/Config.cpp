#include "Config.h"
#include <ArduinoJson.h>

Config::Config() : isValid(false) {}

bool Config::loadFromSD(fs::FS &sd) {
    File configFile = sd.open("/config.json");
    if (!configFile) {
        Serial.println("Failed to open config file");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.print("Failed to parse config: ");
        Serial.println(error.c_str());
        return false;
    }

    wifiSSID = doc["WIFI_SSID"] | "";
    wifiPassword = doc["WIFI_PASS"] | "";
    schedule = doc["SCHEDULE"] | "";
    endpoint = doc["ENDPOINT"] | "";
    endpointType = doc["ENDPOINT_TYPE"] | "";
    endpointUser = doc["ENDPOINT_USER"] | "";
    endpointPassword = doc["ENDPOINT_PASS"] | "";

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
bool Config::valid() const { return isValid; }
