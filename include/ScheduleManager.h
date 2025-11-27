#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <time.h>

class ScheduleManager {
private:
    int uploadHour;  // 0-23, default 12 (noon)
    unsigned long lastUploadTimestamp;
    bool ntpSynced;
    const char* ntpServer;
    int gmtOffsetHours;
    
    unsigned long calculateNextUploadTime();

public:
    ScheduleManager();
    
    bool begin(int uploadHour, int gmtOffsetHours);
    bool syncTime();
    
    bool isUploadTime();
    void markUploadCompleted();
    
    unsigned long getSecondsUntilNextUpload();
    
    bool isTimeSynced() const;
    unsigned long getLastUploadTimestamp() const;
    void setLastUploadTimestamp(unsigned long timestamp);
};

#endif // SCHEDULE_MANAGER_H
