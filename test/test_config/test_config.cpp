#include <unity.h>
#include "Arduino.h"
#include "FS.h"
#include "ArduinoJson.h"

// Include mock implementations
#include "../mocks/Arduino.cpp"

// Mock Logger before including Config
#include "../mocks/MockLogger.h"
#define LOGGER_H  // Prevent real Logger.h from being included

// Mock Preferences before including Config
#include "../mocks/MockPreferences.h"

// Include the Config implementation
#include "Config.h"
#include "../../src/Config.cpp"

fs::FS mockSD;

void setUp(void) {
    // Clear the mock filesystem before each test
    mockSD.clear();
    
    // Clear Preferences data between tests
    Preferences::clearAll();
}

void tearDown(void) {
    // Cleanup after each test
    mockSD.clear();
    Preferences::clearAll();
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

// ============================================================================
// CREDENTIAL SECURITY TESTS (Preferences-based secure storage)
// ============================================================================

// Test loading config with plain text mode enabled
void test_config_plain_text_mode() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "PlainTextPassword",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "PlainEndpointPass",
        "STORE_CREDENTIALS_PLAIN_TEXT": true
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config.valid(), "Config should be valid");
    TEST_ASSERT_TRUE_MESSAGE(config.isStoringPlainText(), "Should be in plain text mode");
    TEST_ASSERT_FALSE_MESSAGE(config.areCredentialsInFlash(), "Credentials should not be in flash");
    TEST_ASSERT_EQUAL_STRING("PlainTextPassword", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("PlainEndpointPass", config.getEndpointPassword().c_str());
}

// Test loading config with secure mode (default behavior)
void test_config_secure_mode_migration() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "SecurePassword123",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "SecureEndpointPass"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_FALSE(config.isStoringPlainText());
    TEST_ASSERT_TRUE(config.areCredentialsInFlash());
    
    // Credentials should be loaded from Preferences
    TEST_ASSERT_EQUAL_STRING("SecurePassword123", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("SecureEndpointPass", config.getEndpointPassword().c_str());
    
    // Config file should be censored
    std::vector<uint8_t> contentVec = mockSD.getFileContent(String("/config.json"));
    std::string updatedConfig(contentVec.begin(), contentVec.end());
    TEST_ASSERT_TRUE(updatedConfig.find("***STORED_IN_FLASH***") != std::string::npos);
}

// Test loading config with already censored credentials
void test_config_secure_mode_already_censored() {
    // First, create a config and let it migrate
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "OriginalPassword",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "OriginalEndpointPass"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        // config1 destructor called here, closing Preferences
    }
    
    // Now create a new config object and load again (simulating reboot)
    // The config file should now be censored and Preferences should have the data
    Config config2;
    bool loaded = config2.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config2.valid());
    TEST_ASSERT_FALSE(config2.isStoringPlainText());
    TEST_ASSERT_TRUE(config2.areCredentialsInFlash());
    
    // Should load credentials from Preferences
    TEST_ASSERT_EQUAL_STRING("OriginalPassword", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("OriginalEndpointPass", config2.getEndpointPassword().c_str());
}

// Test credential storage with various string lengths
void test_config_credential_storage_various_lengths() {
    // Test short password
    std::string shortConfig = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "abc",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "123"
    })";
    
    mockSD.clear();
    mockSD.addFile("/config.json", shortConfig);
    
    Config config1;
    bool loaded1 = config1.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded1);
    TEST_ASSERT_EQUAL_STRING("abc", config1.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("123", config1.getEndpointPassword().c_str());
    
    // Test long password (64 characters)
    std::string longPassword = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";
    std::string longConfig = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": ")" + longPassword + R"(",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": ")" + longPassword + R"("
    })";
    
    mockSD.clear();
    mockSD.addFile("/config.json", longConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded2);
    TEST_ASSERT_EQUAL_STRING(longPassword.c_str(), config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING(longPassword.c_str(), config2.getEndpointPassword().c_str());
    
    // Test password with special characters
    std::string specialPass = "P@ssw0rd!#$%^&*()";
    std::string specialEndpoint = "End!@#$%^&*()_+";
    std::string specialConfig = "{\"WIFI_SSID\":\"TestNetwork\",\"WIFI_PASS\":\"" + specialPass + 
                                "\",\"ENDPOINT\":\"//server/share\",\"ENDPOINT_PASS\":\"" + specialEndpoint + "\"}";
    
    mockSD.clear();
    mockSD.addFile("/config.json", specialConfig);
    
    Config config3;
    bool loaded3 = config3.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded3);
    TEST_ASSERT_EQUAL_STRING(specialPass.c_str(), config3.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING(specialEndpoint.c_str(), config3.getEndpointPassword().c_str());
}

// Test credential retrieval with non-existing keys
void test_config_credential_retrieval_missing_keys() {
    // Create config with censored credentials but no Preferences data
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "***STORED_IN_FLASH***",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "***STORED_IN_FLASH***"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    // Clear any existing Preferences data for this test
    Preferences::clearAll();
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    // Should still load but with empty credentials (fallback behavior)
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_TRUE(config.areCredentialsInFlash());
}

// Test empty credential handling
void test_config_empty_credentials() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": ""
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    // Empty credentials should be handled gracefully
    TEST_ASSERT_EQUAL_STRING("", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("", config.getEndpointPassword().c_str());
}

// Test switching from plain text to secure mode
void test_config_switch_plain_to_secure() {
    // First load with plain text mode
    std::string plainConfig = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "PlainPassword",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "PlainEndpointPass",
        "STORE_CREDENTIALS_PLAIN_TEXT": true
    })";
    
    mockSD.addFile("/config.json", plainConfig);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE_MESSAGE(loaded1, "Plain text config should load");
        TEST_ASSERT_TRUE_MESSAGE(config1.isStoringPlainText(), "Should be in plain text mode");
    }
    
    // Now switch to secure mode by changing the flag
    std::string secureConfig = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "PlainPassword",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "PlainEndpointPass",
        "STORE_CREDENTIALS_PLAIN_TEXT": false
    })";
    
    mockSD.clear();
    mockSD.addFile("/config.json", secureConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    TEST_ASSERT_TRUE_MESSAGE(loaded2, "Secure config should load");
    TEST_ASSERT_FALSE_MESSAGE(config2.isStoringPlainText(), "Should not be in plain text mode");
    TEST_ASSERT_TRUE_MESSAGE(config2.areCredentialsInFlash(), "Credentials should be in flash");
    
    // Credentials should be migrated to Preferences
    TEST_ASSERT_EQUAL_STRING("PlainPassword", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("PlainEndpointPass", config2.getEndpointPassword().c_str());
}

// Test config file censoring accuracy
void test_config_censoring_accuracy() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "ShouldBeCensored",
        "ENDPOINT": "//server/share",
        "ENDPOINT_TYPE": "SMB",
        "ENDPOINT_USER": "testuser",
        "ENDPOINT_PASS": "AlsoCensored",
        "UPLOAD_HOUR": 12
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    
    // Read back the config file
    std::vector<uint8_t> content = mockSD.getFileContent(String("/config.json"));
    std::string fileContent(content.begin(), content.end());
    
    // Verify credentials are censored
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("***STORED_IN_FLASH***") != std::string::npos, 
                             "Should contain censored placeholder");
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("ShouldBeCensored") == std::string::npos, 
                             "Should not contain original WiFi password");
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("AlsoCensored") == std::string::npos, 
                             "Should not contain original endpoint password");
    
    // Verify other fields are preserved
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("TestNetwork") != std::string::npos, 
                             "Should preserve SSID");
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("testuser") != std::string::npos, 
                             "Should preserve username");
    TEST_ASSERT_TRUE_MESSAGE(fileContent.find("SMB") != std::string::npos, 
                             "Should preserve endpoint type");
}

// Test multiple Config instances with Preferences
void test_config_multiple_instances() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "SharedPassword",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "SharedEndpointPass"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    // Create first config instance and let it migrate
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        TEST_ASSERT_EQUAL_STRING("SharedPassword", config1.getWifiPassword().c_str());
        // config1 destructor called here
    }
    
    // Create second config instance (should read from same Preferences)
    // The config file is now censored, so it should load from Preferences
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded2);
    TEST_ASSERT_EQUAL_STRING("SharedPassword", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("SharedEndpointPass", config2.getEndpointPassword().c_str());
}

// Test config with only WiFi password (no endpoint password)
void test_config_wifi_only_secure() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "WIFI_PASS": "WiFiOnlyPassword",
        "ENDPOINT": "//server/share"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_TRUE(config.areCredentialsInFlash());
    TEST_ASSERT_EQUAL_STRING("WiFiOnlyPassword", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("", config.getEndpointPassword().c_str());
}

// Test config with only endpoint password (no WiFi password)
void test_config_endpoint_only_secure() {
    std::string configContent = R"({
        "WIFI_SSID": "TestNetwork",
        "ENDPOINT": "//server/share",
        "ENDPOINT_PASS": "EndpointOnlyPassword"
    })";
    
    mockSD.addFile("/config.json", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_TRUE(config.areCredentialsInFlash());
    TEST_ASSERT_EQUAL_STRING("", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("EndpointOnlyPassword", config.getEndpointPassword().c_str());
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
    
    // Credential security tests (Preferences-based)
    RUN_TEST(test_config_plain_text_mode);
    RUN_TEST(test_config_secure_mode_migration);
    RUN_TEST(test_config_secure_mode_already_censored);
    RUN_TEST(test_config_credential_storage_various_lengths);
    RUN_TEST(test_config_credential_retrieval_missing_keys);
    RUN_TEST(test_config_empty_credentials);
    RUN_TEST(test_config_switch_plain_to_secure);
    RUN_TEST(test_config_censoring_accuracy);
    RUN_TEST(test_config_multiple_instances);
    RUN_TEST(test_config_wifi_only_secure);
    RUN_TEST(test_config_endpoint_only_secure);
    
    return UNITY_END();
}
