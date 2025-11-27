#include <unity.h>
#include "Arduino.h"
#include "FS.h"
#include "ArduinoJson.h"

// Include mock implementations
#include "../mocks/Arduino.cpp"

// Mock Logger before including Config
#include "../mocks/MockLogger.h"
#define LOGGER_H  // Prevent real Logger.h from being included

// Include the Config implementation
#include "Config.h"
#include "../../src/Config.cpp"

fs::FS mockSD;

void setUp(void) {
    // Clear the mock filesystem before each test
    mockSD.clear();
}

void tearDown(void) {
    // Cleanup after each test
    mockSD.clear();
}

// Test loading a valid configuration file
void test_config_load_valid() {
    // Create a valid config.json file
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "TestPassword123",
        "SCHEDULE": "DAILY",
        "ENDPOINT": "//192.168.1.100/share/uploads",
        "ENDPOINT_TYPE": "SMB",
        "ENDPOINT_USER": "testuser",
        "ENDPOINT_PASS": "testpass",
        "UPLOAD_HOUR": 14,
        "SESSION_DURATION_SECONDS": 10,
        "MAX_RETRY_ATTEMPTS": 5,
        "GMT_OFFSET_HOURS": -8
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("TestPassword123", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("DAILY", config.getSchedule().c_str());
    TEST_ASSERT_EQUAL_STRING("//192.168.1.100/share/uploads", config.getEndpoint().c_str());
    TEST_ASSERT_EQUAL_STRING("SMB", config.getEndpointType().c_str());
    TEST_ASSERT_EQUAL_STRING("testuser", config.getEndpointUser().c_str());
    TEST_ASSERT_EQUAL_STRING("testpass", config.getEndpointPassword().c_str());
    TEST_ASSERT_EQUAL(14, config.getUploadHour());
    TEST_ASSERT_EQUAL(10, config.getSessionDurationSeconds());
    TEST_ASSERT_EQUAL(5, config.getMaxRetryAttempts());
    TEST_ASSERT_EQUAL(-8, config.getGmtOffsetHours());
}

// Test loading configuration with default values
void test_config_load_with_defaults() {
    // Create a minimal config.json with only required fields
    std::string configContent = R"({
        "WIFI_SSID": "MinimalNetwork",
        "ENDPOINT": "//server/share"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("MinimalNetwork", config.getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("//server/share", config.getEndpoint().c_str());
    
    // Check default values
    TEST_ASSERT_EQUAL(12, config.getUploadHour());  // Default noon
    TEST_ASSERT_EQUAL(5, config.getSessionDurationSeconds());  // Default 5 seconds
    TEST_ASSERT_EQUAL(3, config.getMaxRetryAttempts());  // Default 3 attempts
    TEST_ASSERT_EQUAL(0, config.getGmtOffsetHours());  // Default UTC
}

// Test loading configuration with missing SSID (should fail)
void test_config_load_missing_ssid() {
    std::string configContent = R"({
        "WIFI_PASS": "password",
        "ENDPOINT": "//server/share"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading configuration with missing endpoint (should fail)
void test_config_load_missing_endpoint() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "password"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading when config file doesn't exist
void test_config_load_file_not_found() {
    // Don't add any file to the mock filesystem
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading invalid JSON
void test_config_load_invalid_json() {
    std::string configContent = "{ invalid json content";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading empty config file
void test_config_load_empty_file() {
    mockSD.addFile("/config.json", "");
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test WebDAV endpoint type
void test_config_webdav_endpoint() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "https://cloud.example.com/remote.php/dav/files/user/",
        "ENDPOINT_TYPE": "WEBDAV",
        "ENDPOINT_USER": "webdavuser",
        "ENDPOINT_PASS": "webdavpass"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("WEBDAV", config.getEndpointType().c_str());
    TEST_ASSERT_EQUAL_STRING("https://cloud.example.com/remote.php/dav/files/user/", config.getEndpoint().c_str());
}

// Test SleepHQ endpoint type
void test_config_sleephq_endpoint() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "https://sleephq.com/api/upload",
        "ENDPOINT_TYPE": "SLEEPHQ",
        "ENDPOINT_USER": "apikey123"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("SLEEPHQ", config.getEndpointType().c_str());
}

// Test configuration with negative GMT offset (e.g., PST)
void test_config_negative_gmt_offset() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "GMT_OFFSET_HOURS": -8
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(-8, config.getGmtOffsetHours());  // -8 hours (PST)
}

// Test configuration with positive GMT offset (e.g., CET)
void test_config_positive_gmt_offset() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "GMT_OFFSET_HOURS": 1
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(1, config.getGmtOffsetHours());  // +1 hour (CET)
}

// Test configuration with various upload hours
void test_config_upload_hours() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "UPLOAD_HOUR": 23
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(23, config.getUploadHour());
}

// Test configuration with long session duration
void test_config_long_session_duration() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "SESSION_DURATION_SECONDS": 300
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(300, config.getSessionDurationSeconds());  // 5 minutes
}

// Test configuration with high retry attempts
void test_config_high_retry_attempts() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "MAX_RETRY_ATTEMPTS": 10
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(10, config.getMaxRetryAttempts());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic loading tests
    RUN_TEST(test_config_load_valid);
    RUN_TEST(test_config_load_with_defaults);
    RUN_TEST(test_config_load_missing_ssid);
    RUN_TEST(test_config_load_missing_endpoint);
    RUN_TEST(test_config_load_file_not_found);
    RUN_TEST(test_config_load_invalid_json);
    RUN_TEST(test_config_load_empty_file);
    
    // Endpoint type tests
    RUN_TEST(test_config_webdav_endpoint);
    RUN_TEST(test_config_sleephq_endpoint);
    
    // Configuration value tests
    RUN_TEST(test_config_negative_gmt_offset);
    RUN_TEST(test_config_positive_gmt_offset);
    RUN_TEST(test_config_upload_hours);
    RUN_TEST(test_config_long_session_duration);
    RUN_TEST(test_config_high_retry_attempts);
    
    return UNITY_END();
}
