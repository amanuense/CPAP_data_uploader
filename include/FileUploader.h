#ifndef FILE_UPLOADER_H
#define FILE_UPLOADER_H

#include <Arduino.h>
#include <FS.h>
#include "Config.h"

class FileUploader {
private:
    Config* config;

public:
    FileUploader(Config* cfg);
    
    bool uploadFile(const String& filePath, fs::FS &sd);
    bool uploadNewFiles(fs::FS &sd);
};

#endif // FILE_UPLOADER_H
