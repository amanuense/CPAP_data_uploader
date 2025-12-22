#include "WiFiManager.h"
#include "Logger.h"
#include <WiFi.h>

WiFiManager::WiFiManager() : connected(false) {}

bool WiFiManager::connectStation(const String& ssid, const String& password) {
    // Validate SSID before attempting connection
    if (ssid.isEmpty()) {
        LOG_ERROR("Cannot connect to WiFi: SSID is empty");
        return false;
    }
    
    if (ssid.length() > 32) {
        LOG_ERROR("Cannot connect to WiFi: SSID exceeds 32 character limit");
        LOGF("SSID length: %d characters", ssid.length());
        return false;
    }
    
    if (password.isEmpty()) {
        LOG_WARN("WiFi password is empty - attempting open network connection");
    }
    
    LOGF("Connecting to WiFi: %s", ssid.c_str());
    LOGF("SSID length: %d characters", ssid.length());

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        LOG_DEBUG(".");
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

int WiFiManager::getSignalStrength() const {
    if (connected && WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();  // Returns signal strength in dBm
    }
    return 0;
}

String WiFiManager::getSignalQuality() const {
    if (!connected || WiFi.status() != WL_CONNECTED) {
        return "Not connected";
    }
    
    int rssi = WiFi.RSSI();
    
    // Classify signal strength based on RSSI value
    // RSSI ranges: Excellent > -50, Good > -60, Fair > -70, Weak > -80, Very Weak <= -80
    if (rssi > -50) {
        return "Excellent";
    } else if (rssi > -60) {
        return "Good";
    } else if (rssi > -70) {
        return "Fair";
    } else if (rssi > -80) {
        return "Weak";
    } else {
        return "Very Weak";
    }
}
