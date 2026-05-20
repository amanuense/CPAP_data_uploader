#include <unity.h>
#include "Arduino.h"
#include "FS.h"

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
    // Create a valid config.txt file
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = TestPassword123\n"
        "SCHEDULE = DAILY\n"
        "ENDPOINT = //192.168.1.100/share/uploads\n"
        "ENDPOINT_TYPE = SMB\n"
        "ENDPOINT_USER = testuser\n"
        "ENDPOINT_PASSWORD = testpass\n"
        "UPLOAD_MODE = scheduled\n"
        "UPLOAD_START_HOUR = 14\n"
        "UPLOAD_END_HOUR = 16\n"
        "INACTIVITY_SECONDS = 140\n"
        "EXCLUSIVE_ACCESS_MINUTES = 10\n"
        "COOLDOWN_MINUTES = 12\n"
        "GMT_OFFSET_HOURS = -8\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    TEST_ASSERT_EQUAL_STRING("scheduled", config.getUploadMode().c_str());
    TEST_ASSERT_EQUAL(14, config.getUploadStartHour());
    TEST_ASSERT_EQUAL(16, config.getUploadEndHour());
    TEST_ASSERT_EQUAL(140, config.getInactivitySeconds());
    TEST_ASSERT_EQUAL(10, config.getExclusiveAccessMinutes());
    TEST_ASSERT_EQUAL(12, config.getCooldownMinutes());
    TEST_ASSERT_EQUAL(-8, config.getGmtOffsetHours());
}

// Test loading configuration with default values
void test_config_load_with_defaults() {
    // Create a minimal config.txt with only required fields
    std::string configContent = 
        "WIFI_SSID = MinimalNetwork\n"
        "ENDPOINT = //server/share\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("MinimalNetwork", config.getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("//server/share", config.getEndpoint().c_str());
    
    // Check default values
    TEST_ASSERT_EQUAL_STRING("smart", config.getUploadMode().c_str());
    TEST_ASSERT_EQUAL(9, config.getUploadStartHour());
    TEST_ASSERT_EQUAL(21, config.getUploadEndHour());
    TEST_ASSERT_EQUAL(62, config.getInactivitySeconds());
    TEST_ASSERT_EQUAL(5, config.getExclusiveAccessMinutes());
    TEST_ASSERT_EQUAL(10, config.getCooldownMinutes());
    TEST_ASSERT_EQUAL(0, config.getGmtOffsetHours());  // Default UTC
    TEST_ASSERT_FALSE(config.getLogToSdCard());  // Default false (no SD logging)
}

// Test loading configuration with missing SSID (should fail)
void test_config_load_missing_ssid() {
    std::string configContent = 
        "WIFI_PASSWORD = password\n"
        "ENDPOINT = //server/share\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading configuration with missing endpoint (should fail)
void test_config_load_missing_endpoint() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = password\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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

// Test loading invalid format (should just skip invalid lines)
void test_config_load_invalid_format() {
    std::string configContent = 
        "WIFI_SSID = ValidSSID\n"
        "This line is invalid and has no equals sign\n"
        "ENDPOINT = //server/share\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    // Should still load valid parts
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("ValidSSID", config.getWifiSSID().c_str());
}

// Test loading empty config file
void test_config_load_empty_file() {
    mockSD.addFile("/config.txt", "");
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_FALSE(loaded);
    TEST_ASSERT_FALSE(config.valid());
}

// Test loading configuration with webhook fields
void test_config_load_webhooks() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "SLEEPLAB_DOMAIN = https://sleeplab.example.com/\n"
        "SLEEPLAB_USER_ID = user_abc123\n"
        "SLEEPLAB_SECRET = topsecret\n"
        "GENERIC_WEBHOOK_URL = https://hc-ping.com/uuid-here\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("https://sleeplab.example.com", config.getSleepLabDomain().c_str());
    TEST_ASSERT_EQUAL_STRING("user_abc123", config.getSleepLabUserId().c_str());
    TEST_ASSERT_EQUAL_STRING("topsecret", config.getSleepLabSecret().c_str());
    TEST_ASSERT_EQUAL_STRING("https://hc-ping.com/uuid-here", config.getGenericWebhookUrl().c_str());
    TEST_ASSERT_TRUE(config.getWebhookExtendedMetadata());
    TEST_ASSERT_FALSE(config.getWebhookAppendFailPath());
}

// Test loading configuration with webhook boolean toggles
void test_config_load_webhooks_toggles() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "WEBHOOK_EXTENDED_METADATA = false\n"
        "WEBHOOK_APPEND_FAIL_PATH = true\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_FALSE(config.getWebhookExtendedMetadata());
    TEST_ASSERT_TRUE(config.getWebhookAppendFailPath());
}

// Test loading configuration without webhook fields (should default to empty)
void test_config_load_webhooks_empty() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("", config.getSleepLabDomain().c_str());
    TEST_ASSERT_EQUAL_STRING("", config.getSleepLabUserId().c_str());
    TEST_ASSERT_EQUAL_STRING("", config.getSleepLabSecret().c_str());
    TEST_ASSERT_EQUAL_STRING("", config.getGenericWebhookUrl().c_str());
    TEST_ASSERT_TRUE(config.getWebhookExtendedMetadata());
    TEST_ASSERT_FALSE(config.getWebhookAppendFailPath());
}

// Test WebDAV endpoint type
void test_config_webdav_endpoint() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = https://cloud.example.com/remote.php/dav/files/user/\n"
        "ENDPOINT_TYPE = WEBDAV\n"
        "ENDPOINT_USER = webdavuser\n"
        "ENDPOINT_PASSWORD = webdavpass\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("WEBDAV", config.getEndpointType().c_str());
    TEST_ASSERT_EQUAL_STRING("https://cloud.example.com/remote.php/dav/files/user/", config.getEndpoint().c_str());
}

// Test SleepHQ endpoint type (cloud endpoint requires CLOUD_CLIENT_ID)
void test_config_sleephq_endpoint() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT_TYPE = SLEEPHQ\n"
        "CLOUD_CLIENT_ID = test_client_id\n"
        "CLOUD_CLIENT_SECRET = test_secret\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_EQUAL_STRING("SLEEPHQ", config.getEndpointType().c_str());
    TEST_ASSERT_TRUE(config.hasCloudEndpoint());
}

// Test configuration with negative GMT offset (e.g., PST)
void test_config_negative_gmt_offset() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "GMT_OFFSET_HOURS = -8\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(-8, config.getGmtOffsetHours());  // -8 hours (PST)
}

// Test configuration with positive GMT offset (e.g., CET)
void test_config_positive_gmt_offset() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "GMT_OFFSET_HOURS = 1\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(1, config.getGmtOffsetHours());  // +1 hour (CET)
}

// Test configuration with upload window hours
void test_config_upload_window_hours() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "UPLOAD_START_HOUR = 23\n"
        "UPLOAD_END_HOUR = 5\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(23, config.getUploadStartHour());
    TEST_ASSERT_EQUAL(5, config.getUploadEndHour());
}

// Test configuration with exclusive access duration
void test_config_exclusive_access_minutes() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "EXCLUSIVE_ACCESS_MINUTES = 15\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(15, config.getExclusiveAccessMinutes());
}

// Test SD card logging configuration
void test_config_boot_delay_and_logging() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "LOG_TO_SD_CARD = true\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.getLogToSdCard());
}

// Test configuration with all FSM timing fields
void test_config_all_fsm_timing_fields() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "UPLOAD_MODE = smart\n"
        "UPLOAD_START_HOUR = 7\n"
        "UPLOAD_END_HOUR = 21\n"
        "INACTIVITY_SECONDS = 180\n"
        "EXCLUSIVE_ACCESS_MINUTES = 6\n"
        "COOLDOWN_MINUTES = 9\n"
        "LOG_TO_SD_CARD = false\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("smart", config.getUploadMode().c_str());
    TEST_ASSERT_EQUAL(7, config.getUploadStartHour());
    TEST_ASSERT_EQUAL(21, config.getUploadEndHour());
    TEST_ASSERT_EQUAL(180, config.getInactivitySeconds());
    TEST_ASSERT_EQUAL(6, config.getExclusiveAccessMinutes());
    TEST_ASSERT_EQUAL(9, config.getCooldownMinutes());
    TEST_ASSERT_FALSE(config.getLogToSdCard());
}


// ============================================================================
// CREDENTIAL SECURITY TESTS (Preferences-based secure storage)
// ============================================================================

// Test loading config with plain text mode enabled
void test_config_plain_text_mode() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = PlainTextPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = PlainEndpointPass\n"
        "STORE_CREDENTIALS_PLAIN_TEXT = true\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = SecurePassword123\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = SecureEndpointPass\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::vector<uint8_t> contentVec = mockSD.getFileContent(String("/config.txt"));
    std::string updatedConfig(contentVec.begin(), contentVec.end());
    TEST_ASSERT_TRUE(updatedConfig.find("***STORED_IN_FLASH***") != std::string::npos);
}

// Test loading config with already censored credentials
void test_config_secure_mode_already_censored() {
    // First, create a config and let it migrate
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = OriginalPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = OriginalEndpointPass\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        // config1 destructor called here, closing Preferences
    }
    
    // Now create a new config object and load again (simulating reboot)
    // The config file is now censored, so it should load from Preferences
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
    std::string shortConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = abc\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = 123\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", shortConfig);
    
    Config config1;
    bool loaded1 = config1.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded1);
    TEST_ASSERT_EQUAL_STRING("abc", config1.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("123", config1.getEndpointPassword().c_str());
    
    // Test long password (64 characters)
    std::string longPassword = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";
    std::string longConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = " + longPassword + "\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = " + longPassword + "\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", longConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded2);
    TEST_ASSERT_EQUAL_STRING(longPassword.c_str(), config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING(longPassword.c_str(), config2.getEndpointPassword().c_str());
    
    // Test password with special characters
    std::string specialPass = "P@ssw0rd!#$%^&*()";
    std::string specialEndpoint = "End!@#$%^&*()_+";
    std::string specialConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = " + specialPass + "\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = " + specialEndpoint + "\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", specialConfig);
    
    Config config3;
    bool loaded3 = config3.loadFromSD(mockSD);
    TEST_ASSERT_TRUE(loaded3);
    TEST_ASSERT_EQUAL_STRING(specialPass.c_str(), config3.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING(specialEndpoint.c_str(), config3.getEndpointPassword().c_str());
}

// Test credential retrieval with non-existing keys
void test_config_credential_retrieval_missing_keys() {
    // Create config with censored credentials but no Preferences data
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = ***STORED_IN_FLASH***\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = ***STORED_IN_FLASH***\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = \n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = \n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::string plainConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = PlainPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = PlainEndpointPass\n"
        "STORE_CREDENTIALS_PLAIN_TEXT = true\n";
    
    mockSD.addFile("/config.txt", plainConfig);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE_MESSAGE(loaded1, "Plain text config should load");
        TEST_ASSERT_TRUE_MESSAGE(config1.isStoringPlainText(), "Should be in plain text mode");
    }
    
    // Now switch to secure mode by changing the flag
    std::string secureConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = PlainPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = PlainEndpointPass\n"
        "STORE_CREDENTIALS_PLAIN_TEXT = false\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", secureConfig);
    
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = ShouldBeCensored\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_TYPE = SMB\n"
        "ENDPOINT_USER = testuser\n"
        "ENDPOINT_PASSWORD = AlsoCensored\n"
        "UPLOAD_MODE = scheduled\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    
    // Read back the config file
    std::vector<uint8_t> content = mockSD.getFileContent(String("/config.txt"));
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = SharedPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = SharedEndpointPass\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = WiFiOnlyPassword\n"
        "ENDPOINT = //server/share\n";
    
    mockSD.addFile("/config.txt", configContent);
    
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
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = EndpointOnlyPassword\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    TEST_ASSERT_TRUE(config.areCredentialsInFlash());
    TEST_ASSERT_EQUAL_STRING("", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("EndpointOnlyPassword", config.getEndpointPassword().c_str());
}

// ============================================================================
// INDIVIDUAL CREDENTIAL UPDATE TESTS
// ============================================================================

// Test updating only WiFi password (endpoint remains censored)
void test_config_update_wifi_only() {
    // First, create initial config and let it migrate to secure storage
    std::string initialConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = OriginalWiFiPass\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = OriginalEndpointPass\n";
    
    mockSD.addFile("/config.txt", initialConfig);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        // Let migration happen and config file get censored
    }
    
    // Now simulate user updating only WiFi password in config.txt
    std::string updatedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = NewWiFiPassword123\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = ***STORED_IN_FLASH***\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", updatedConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded2, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config2.valid(), "Config should be valid");
    
    // Should use new WiFi password from config, stored endpoint password from flash
    TEST_ASSERT_EQUAL_STRING("NewWiFiPassword123", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("OriginalEndpointPass", config2.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config2.areCredentialsInFlash(), "Should have credentials in flash");
}

// Test updating only endpoint password (WiFi remains censored)
void test_config_update_endpoint_only() {
    // First, create initial config and let it migrate to secure storage
    std::string initialConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = OriginalWiFiPass\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = OriginalEndpointPass\n";
    
    mockSD.addFile("/config.txt", initialConfig);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        // Let migration happen and config file get censored
    }
    
    // Now simulate user updating only endpoint password in config.txt
    std::string updatedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = ***STORED_IN_FLASH***\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = NewEndpointPassword456\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", updatedConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded2, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config2.valid(), "Config should be valid");
    
    // Should use stored WiFi password from flash, new endpoint password from config
    TEST_ASSERT_EQUAL_STRING("OriginalWiFiPass", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("NewEndpointPassword456", config2.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config2.areCredentialsInFlash(), "Should have credentials in flash");
}

// Test updating both credentials (both plain text in config)
void test_config_update_both_credentials() {
    // First, create initial config and let it migrate to secure storage
    std::string initialConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = OriginalWiFiPass\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = OriginalEndpointPass\n";
    
    mockSD.addFile("/config.txt", initialConfig);
    
    {
        Config config1;
        bool loaded1 = config1.loadFromSD(mockSD);
        TEST_ASSERT_TRUE(loaded1);
        // Let migration happen and config file get censored
    }
    
    // Now simulate user updating both passwords in config.txt
    std::string updatedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = NewWiFiPassword123\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = NewEndpointPassword456\n";
    
    mockSD.clear();
    mockSD.addFile("/config.txt", updatedConfig);
    
    Config config2;
    bool loaded2 = config2.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded2, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config2.valid(), "Config should be valid");
    
    // Should use both new passwords from config
    TEST_ASSERT_EQUAL_STRING("NewWiFiPassword123", config2.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("NewEndpointPassword456", config2.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config2.areCredentialsInFlash(), "Should have credentials in flash after migration");
}

// Test mixed state: WiFi password new, endpoint censored (starting from fresh)
void test_config_mixed_state_wifi_new() {
    // Pre-populate Preferences with endpoint password
    {
        Preferences prefs;
        prefs.begin("cpap_creds", false);
        prefs.putString("endpoint_pass", "StoredEndpointPass");
        prefs.end();
    }
    
    std::string mixedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = NewWiFiPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = ***STORED_IN_FLASH***\n";
    
    mockSD.addFile("/config.txt", mixedConfig);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config.valid(), "Config should be valid");
    
    // Should use new WiFi password from config, stored endpoint password from flash
    TEST_ASSERT_EQUAL_STRING("NewWiFiPassword", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("StoredEndpointPass", config.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config.areCredentialsInFlash(), "Should have credentials in flash");
}

// Test mixed state: endpoint password new, WiFi censored (starting from fresh)
void test_config_mixed_state_endpoint_new() {
    // Pre-populate Preferences with WiFi password
    {
        Preferences prefs;
        prefs.begin("cpap_creds", false);
        prefs.putString("wifi_pass", "StoredWiFiPass");
        prefs.end();
    }
    
    std::string mixedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = ***STORED_IN_FLASH***\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = NewEndpointPassword\n";
    
    mockSD.addFile("/config.txt", mixedConfig);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config.valid(), "Config should be valid");
    
    // Should use stored WiFi password from flash, new endpoint password from config
    TEST_ASSERT_EQUAL_STRING("StoredWiFiPass", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("NewEndpointPassword", config.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config.areCredentialsInFlash(), "Should have credentials in flash");
}

// Test mixed state: both passwords new (overriding stored ones)
void test_config_mixed_state_both_new() {
    // Pre-populate Preferences with old passwords (should be overridden)
    {
        Preferences prefs;
        prefs.begin("cpap_creds", false);
        prefs.putString("wifi_pass", "OldStoredWiFiPass");
        prefs.putString("endpoint_pass", "OldStoredEndpointPass");
        prefs.end();
    }
    
    std::string mixedConfig = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = NewWiFiPassword\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_PASSWORD = NewEndpointPassword\n";
    
    mockSD.addFile("/config.txt", mixedConfig);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE_MESSAGE(loaded, "Config should load successfully");
    TEST_ASSERT_TRUE_MESSAGE(config.valid(), "Config should be valid");
    
    // Should use new passwords from config (prioritized over stored ones)
    TEST_ASSERT_EQUAL_STRING("NewWiFiPassword", config.getWifiPassword().c_str());
    TEST_ASSERT_EQUAL_STRING("NewEndpointPassword", config.getEndpointPassword().c_str());
    TEST_ASSERT_TRUE_MESSAGE(config.areCredentialsInFlash(), "Should have credentials in flash");
}

// Test power management configuration with default values
void test_config_power_management_defaults() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = password\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_TYPE = SMB\n"
        "ENDPOINT_USER = user\n"
        "ENDPOINT_PASSWORD = pass\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    
    // Test default power management values
    TEST_ASSERT_EQUAL(240, config.getCpuSpeedMhz());
    TEST_ASSERT_EQUAL(WifiTxPower::POWER_HIGH, config.getWifiTxPower());
    TEST_ASSERT_EQUAL(WifiPowerSaving::SAVE_NONE, config.getWifiPowerSaving());
}

// Test power management configuration with custom values
void test_config_power_management_custom() {
    std::string configContent = 
        "WIFI_SSID = TestNetwork\n"
        "WIFI_PASSWORD = password\n"
        "ENDPOINT = //server/share\n"
        "ENDPOINT_TYPE = SMB\n"
        "ENDPOINT_USER = user\n"
        "ENDPOINT_PASSWORD = pass\n"
        "CPU_SPEED_MHZ = 160\n"
        "WIFI_TX_PWR = mid\n"
        "WIFI_PWR_SAVING = max\n";
    
    mockSD.addFile("/config.txt", configContent);
    
    Config config;
    bool loaded = config.loadFromSD(mockSD);
    
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(config.valid());
    
    // Test custom power management values
    TEST_ASSERT_EQUAL(160, config.getCpuSpeedMhz());
    TEST_ASSERT_EQUAL(WifiTxPower::POWER_MID, config.getWifiTxPower());
    TEST_ASSERT_EQUAL(WifiPowerSaving::SAVE_MAX, config.getWifiPowerSaving());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_config_load_valid);
    RUN_TEST(test_config_load_with_defaults);
    RUN_TEST(test_config_load_missing_ssid);
    RUN_TEST(test_config_load_missing_endpoint);
    RUN_TEST(test_config_load_file_not_found);
    RUN_TEST(test_config_load_invalid_format);
    RUN_TEST(test_config_load_empty_file);
    RUN_TEST(test_config_load_webhooks);
    RUN_TEST(test_config_load_webhooks_empty);
    RUN_TEST(test_config_load_webhooks_toggles);
    RUN_TEST(test_config_webdav_endpoint);
    RUN_TEST(test_config_sleephq_endpoint);
    
    RUN_TEST(test_config_negative_gmt_offset);
    RUN_TEST(test_config_positive_gmt_offset);
    RUN_TEST(test_config_upload_window_hours);
    RUN_TEST(test_config_exclusive_access_minutes);
    RUN_TEST(test_config_boot_delay_and_logging);
    RUN_TEST(test_config_all_fsm_timing_fields);
    
    // Credential security tests
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
    
    // Individual credential update tests
    RUN_TEST(test_config_update_wifi_only);
    RUN_TEST(test_config_update_endpoint_only);
    RUN_TEST(test_config_update_both_credentials);
    RUN_TEST(test_config_mixed_state_wifi_new);
    RUN_TEST(test_config_mixed_state_endpoint_new);
    RUN_TEST(test_config_mixed_state_both_new);
    
    // Power management tests
    RUN_TEST(test_config_power_management_defaults);
    RUN_TEST(test_config_power_management_custom);
    
    UNITY_END();
    
    return 0;
}
