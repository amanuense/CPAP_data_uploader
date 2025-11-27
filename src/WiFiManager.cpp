#include "WiFiManager.h"
#include "Logger.h"
#include <WiFi.h>

WiFiManager::WiFiManager() : connected(false) {}

bool WiFiManager::connectStation(const String& ssid, const String& password) {
    LOGF("Connecting to WiFi: %s", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        LOG(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG("\nWiFi connected");
        LOGF("IP address: %s", WiFi.localIP().toString().c_str());
        connected = true;
        return true;
    } else {
        LOG("\nWiFi connection failed");
        connected = false;
        return false;
    }
}

bool WiFiManager::isConnected() const { 
    return connected && WiFi.status() == WL_CONNECTED; 
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    connected = false;
}

String WiFiManager::getIPAddress() const {
    if (connected && WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "Not connected";
}
