#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

class WiFiManager {
private:
    bool connected;

public:
    WiFiManager();
    
    bool connectStation(const String& ssid, const String& password);
    bool isConnected() const;
    void disconnect();
};

#endif // WIFI_MANAGER_H
