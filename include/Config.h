#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FS.h>

class Config {
private:
    String wifiSSID;
    String wifiPassword;
    String schedule;
    String endpoint;
    String endpointType;  // SMB, WEBDAV, SLEEPHQ
    String endpointUser;
    String endpointPassword;
    bool isValid;

public:
    Config();
    
    bool loadFromSD(fs::FS &sd);
    
    const String& getWifiSSID() const;
    const String& getWifiPassword() const;
    const String& getSchedule() const;
    const String& getEndpoint() const;
    const String& getEndpointType() const;
    const String& getEndpointUser() const;
    const String& getEndpointPassword() const;
    bool valid() const;
};

#endif // CONFIG_H
