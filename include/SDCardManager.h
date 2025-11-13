#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <Arduino.h>
#include <FS.h>

class SDCardManager {
private:
    bool initialized;
    bool espHasControl;

    void setControlPin(bool espControl);

public:
    SDCardManager();
    
    bool begin();
    bool takeControl();
    void releaseControl();
    bool hasControl() const;
    fs::FS& getFS();
};

#endif // SDCARD_MANAGER_H
