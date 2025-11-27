#include "SleepHQUploader.h"
#include "Logger.h"

#ifdef ENABLE_SLEEPHQ_UPLOAD

// TODO: Implementation pending
// This is a placeholder for future SleepHQ direct upload support

SleepHQUploader::SleepHQUploader(const String& endpoint, const String& user, const String& apiKey)
    : userId(user), apiKey(apiKey), authenticated(false) {
    parseEndpoint(endpoint);
}

SleepHQUploader::~SleepHQUploader() {
    end();
}

bool SleepHQUploader::parseEndpoint(const String& endpoint) {
    // TODO: Parse SleepHQ API endpoint
    apiEndpoint = endpoint;
    LOG("[SleepHQ] TODO: SleepHQ uploader not yet implemented");
    return false;
}

bool SleepHQUploader::authenticate() {
    LOG("[SleepHQ] ERROR: SleepHQ uploader not yet implemented");
    return false;
}

void SleepHQUploader::disconnect() {
    authenticated = false;
}

bool SleepHQUploader::begin() {
    LOG("[SleepHQ] ERROR: SleepHQ uploader not yet implemented");
    LOG("[SleepHQ] Please use SMB upload or wait for SleepHQ implementation");
    return false;
}

void SleepHQUploader::end() {
    disconnect();
}

bool SleepHQUploader::isConnected() const {
    return authenticated;
}

bool SleepHQUploader::upload(const String& localPath, const String& remotePath, 
                             fs::FS &sd, unsigned long& bytesTransferred) {
    bytesTransferred = 0;
    LOG("[SleepHQ] ERROR: SleepHQ uploader not yet implemented");
    LOG("[SleepHQ] Please use SMB upload or wait for SleepHQ implementation");
    return false;
}

#endif // ENABLE_SLEEPHQ_UPLOAD
