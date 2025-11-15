#include <unity.h>
#include "Arduino.h"
#include "MockTime.h"

// Include mock implementations
#include "../mocks/Arduino.cpp"

// Mock ESP32-specific time functions
static bool mockNtpSyncSuccess = true;
static long mockGmtOffset = 0;
static int mockDaylightOffset = 0;

// Mock configTime function
void configTime(long gmtOffset_sec, int daylightOffset_sec, const char* server1, 
                const char* server2 = nullptr, const char* server3 = nullptr) {
    mockGmtOffset = gmtOffset_sec;
    mockDaylightOffset = daylightOffset_sec;
    // In real implementation, this would configure NTP
}

// Mock getLocalTime function
bool getLocalTime(struct tm* info, uint32_t ms = 5000) {
    if (!mockNtpSyncSuccess) {
        return false;
    }
    
    time_t now = MockTimeState::getTime();
    if (now <= 24 * 3600) {  // Not synced yet
        return false;
    }
    
    // Apply timezone offset to get local time
    time_t localTime = now + mockGmtOffset + mockDaylightOffset;
    
    // Convert to tm struct using gmtime (since we already adjusted for timezone)
    struct tm* timeinfo = gmtime(&localTime);
    if (timeinfo && info) {
        *info = *timeinfo;
        return true;
    }
    return false;
}

// Mock localtime_r function
struct tm* localtime_r(const time_t* timep, struct tm* result) {
    if (!timep || !result) {
        return nullptr;
    }
    
    time_t adjusted = *timep + mockGmtOffset + mockDaylightOffset;
    struct tm* timeinfo = gmtime(&adjusted);
    if (timeinfo) {
        *result = *timeinfo;
        return result;
    }
    return nullptr;
}

// Mock mktime function - converts local time to UTC
time_t mktime(struct tm* timeptr) {
    if (!timeptr) {
        return -1;
    }
    
    // Use timegm to get UTC time, then subtract timezone offset
    #ifdef _WIN32
    time_t utcTime = _mkgmtime(timeptr);
    #else
    time_t utcTime = timegm(timeptr);
    #endif
    
    // Subtract the timezone offset to get back to UTC
    return utcTime - mockGmtOffset - mockDaylightOffset;
}

// Include the ScheduleManager implementation
#include "ScheduleManager.h"
#include "../../src/ScheduleManager.cpp"

void setUp(void) {
    // Reset time and mock state before each test
    MockTimeState::reset();
    mockNtpSyncSuccess = true;
    mockGmtOffset = 0;
    mockDaylightOffset = 0;
}

void tearDown(void) {
    // Cleanup after each test
}

// Helper function to create a UTC timestamp for a specific date/time
time_t makeTimestamp(int year, int month, int day, int hour, int min, int sec) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;  // Years since 1900
    timeinfo.tm_mon = month - 1;     // Months since January (0-11)
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = min;
    timeinfo.tm_sec = sec;
    timeinfo.tm_isdst = 0;
    
    // Use timegm for UTC time (or emulate it)
    #ifdef _WIN32
    return _mkgmtime(&timeinfo);
    #else
    return timegm(&timeinfo);
    #endif
}

// Test next upload time calculation for various scenarios
void test_next_upload_time_before_upload_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon, no timezone offset
    
    // Set time to 10:00 AM on Nov 14, 2025
    time_t currentTime = makeTimestamp(2025, 11, 14, 10, 0, 0);
    MockTimeState::setTime(currentTime);
    
    // Simulate NTP sync
    mockNtpSyncSuccess = true;
    TEST_ASSERT_TRUE(manager.syncTime());
    
    // Next upload should be today at noon (2 hours from now)
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(2 * 3600, secondsUntilNext);  // 2 hours = 7200 seconds
}

void test_next_upload_time_after_upload_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon
    
    // Set time to 2:00 PM on Nov 14, 2025 (after upload hour)
    time_t currentTime = makeTimestamp(2025, 11, 14, 14, 0, 0);
    MockTimeState::setTime(currentTime);
    
    // Simulate NTP sync
    mockNtpSyncSuccess = true;
    TEST_ASSERT_TRUE(manager.syncTime());
    
    // Next upload should be tomorrow at noon (22 hours from now)
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(22 * 3600, secondsUntilNext);  // 22 hours
}

void test_next_upload_time_at_upload_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon
    
    // Set time to exactly noon on Nov 14, 2025
    time_t currentTime = makeTimestamp(2025, 11, 14, 12, 0, 0);
    MockTimeState::setTime(currentTime);
    
    // Simulate NTP sync
    mockNtpSyncSuccess = true;
    TEST_ASSERT_TRUE(manager.syncTime());
    
    // Should be upload time now
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_next_upload_time_different_hours() {
    ScheduleManager manager;
    
    // Test upload at 6 AM
    manager.begin(6, 0, 0);
    time_t currentTime = makeTimestamp(2025, 11, 14, 4, 0, 0);  // 4 AM
    MockTimeState::setTime(currentTime);
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(2 * 3600, secondsUntilNext);  // 2 hours until 6 AM
    
    // Test upload at 11 PM (23:00)
    manager.begin(23, 0, 0);
    currentTime = makeTimestamp(2025, 11, 14, 20, 0, 0);  // 8 PM
    MockTimeState::setTime(currentTime);
    manager.syncTime();
    
    secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(3 * 3600, secondsUntilNext);  // 3 hours until 11 PM
}

// Test upload time detection (before/after configured hour)
void test_is_upload_time_before_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon
    
    // Set time to 10:00 AM
    time_t currentTime = makeTimestamp(2025, 11, 14, 10, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Should not be upload time yet
    TEST_ASSERT_FALSE(manager.isUploadTime());
}

void test_is_upload_time_during_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon
    
    // Set time to 12:30 PM (during upload hour)
    time_t currentTime = makeTimestamp(2025, 11, 14, 12, 30, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Should be upload time
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_is_upload_time_after_hour() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);  // Upload at noon
    
    // Set time to 2:00 PM (after upload hour)
    time_t currentTime = makeTimestamp(2025, 11, 14, 14, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Should not be upload time anymore
    TEST_ASSERT_FALSE(manager.isUploadTime());
}

void test_is_upload_time_without_sync() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Don't sync time
    mockNtpSyncSuccess = false;
    
    // Should not be upload time if not synced
    TEST_ASSERT_FALSE(manager.isUploadTime());
}

// Test schedule window logic (current day vs next day)
void test_schedule_window_current_day() {
    ScheduleManager manager;
    manager.begin(15, 0, 0);  // Upload at 3 PM
    
    // Set time to 10:00 AM (before upload hour)
    time_t currentTime = makeTimestamp(2025, 11, 14, 10, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Next upload should be today
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(5 * 3600, secondsUntilNext);  // 5 hours until 3 PM
}

void test_schedule_window_next_day() {
    ScheduleManager manager;
    manager.begin(15, 0, 0);  // Upload at 3 PM
    
    // Set time to 4:00 PM (after upload hour)
    time_t currentTime = makeTimestamp(2025, 11, 14, 16, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Next upload should be tomorrow at 3 PM (23 hours from now)
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(23 * 3600, secondsUntilNext);  // 23 hours
}

void test_schedule_window_midnight_crossing() {
    ScheduleManager manager;
    manager.begin(2, 0, 0);  // Upload at 2 AM
    
    // Set time to 11:00 PM (before midnight)
    time_t currentTime = makeTimestamp(2025, 11, 14, 23, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Next upload should be tomorrow at 2 AM (3 hours from now)
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(3 * 3600, secondsUntilNext);  // 3 hours
}

// Test timestamp tracking and update
void test_timestamp_tracking_initial() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Initially, last upload timestamp should be 0
    TEST_ASSERT_EQUAL(0, manager.getLastUploadTimestamp());
}

void test_timestamp_tracking_after_mark() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set current time
    time_t currentTime = makeTimestamp(2025, 11, 14, 12, 30, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Mark upload as completed
    manager.markUploadCompleted();
    
    // Last upload timestamp should be set to current time
    TEST_ASSERT_EQUAL(currentTime, manager.getLastUploadTimestamp());
}

void test_timestamp_prevents_duplicate_upload() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set time to noon
    time_t currentTime = makeTimestamp(2025, 11, 14, 12, 30, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Should be upload time
    TEST_ASSERT_TRUE(manager.isUploadTime());
    
    // Mark upload as completed
    manager.markUploadCompleted();
    
    // Should no longer be upload time (already uploaded today)
    TEST_ASSERT_FALSE(manager.isUploadTime());
}

void test_timestamp_allows_next_day_upload() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set time to noon on Nov 14
    time_t currentTime = makeTimestamp(2025, 11, 14, 12, 30, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    manager.syncTime();
    
    // Mark upload as completed
    manager.markUploadCompleted();
    
    // Should not be upload time anymore
    TEST_ASSERT_FALSE(manager.isUploadTime());
    
    // Advance to next day at noon
    currentTime = makeTimestamp(2025, 11, 15, 12, 30, 0);
    MockTimeState::setTime(currentTime);
    
    // Should be upload time again (new day)
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_timestamp_set_manually() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set timestamp manually
    time_t manualTime = makeTimestamp(2025, 11, 13, 12, 0, 0);
    manager.setLastUploadTimestamp(manualTime);
    
    // Verify it was set
    TEST_ASSERT_EQUAL(manualTime, manager.getLastUploadTimestamp());
}

// Test timezone offset handling
void test_timezone_offset_positive() {
    ScheduleManager manager;
    
    // GMT+5 (5 hours ahead)
    long gmtOffset = 5 * 3600;
    manager.begin(12, gmtOffset, 0);
    
    // Set UTC time to 7:00 AM (which is noon in GMT+5)
    time_t utcTime = makeTimestamp(2025, 11, 14, 7, 0, 0);
    MockTimeState::setTime(utcTime);
    
    mockNtpSyncSuccess = true;
    mockGmtOffset = gmtOffset;
    manager.syncTime();
    
    // Should be upload time (local time is noon)
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_timezone_offset_negative() {
    ScheduleManager manager;
    
    // GMT-8 (8 hours behind, like PST)
    long gmtOffset = -8 * 3600;
    manager.begin(12, gmtOffset, 0);
    
    // Set UTC time to 8:00 PM (which is noon in GMT-8)
    time_t utcTime = makeTimestamp(2025, 11, 14, 20, 0, 0);
    MockTimeState::setTime(utcTime);
    
    mockNtpSyncSuccess = true;
    mockGmtOffset = gmtOffset;
    manager.syncTime();
    
    // Should be upload time (local time is noon)
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_timezone_offset_with_daylight_saving() {
    ScheduleManager manager;
    
    // GMT-5 with +1 hour DST (like EDT)
    long gmtOffset = -5 * 3600;
    int daylightOffset = 3600;
    manager.begin(12, gmtOffset, daylightOffset);
    
    // Set UTC time to 4:00 PM (which is noon in GMT-5+1)
    time_t utcTime = makeTimestamp(2025, 11, 14, 16, 0, 0);
    MockTimeState::setTime(utcTime);
    
    mockNtpSyncSuccess = true;
    mockGmtOffset = gmtOffset;
    mockDaylightOffset = daylightOffset;
    manager.syncTime();
    
    // Should be upload time (local time is noon)
    TEST_ASSERT_TRUE(manager.isUploadTime());
}

void test_timezone_offset_calculation() {
    ScheduleManager manager;
    
    // GMT+3
    long gmtOffset = 3 * 3600;
    manager.begin(15, gmtOffset, 0);  // Upload at 3 PM local time
    
    // Set UTC time to 10:00 AM (which is 1 PM in GMT+3)
    time_t utcTime = makeTimestamp(2025, 11, 14, 10, 0, 0);
    MockTimeState::setTime(utcTime);
    
    mockNtpSyncSuccess = true;
    mockGmtOffset = gmtOffset;
    manager.syncTime();
    
    // Next upload should be in 2 hours (at 3 PM local = noon UTC)
    unsigned long secondsUntilNext = manager.getSecondsUntilNextUpload();
    TEST_ASSERT_EQUAL(2 * 3600, secondsUntilNext);
}

// Test NTP sync behavior
void test_ntp_sync_success() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set time to a valid value (past Jan 1, 1970 + 1 day)
    time_t currentTime = makeTimestamp(2025, 11, 14, 10, 0, 0);
    MockTimeState::setTime(currentTime);
    
    mockNtpSyncSuccess = true;
    
    // Sync should succeed
    TEST_ASSERT_TRUE(manager.syncTime());
    TEST_ASSERT_TRUE(manager.isTimeSynced());
}

void test_ntp_sync_failure() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Set time to invalid value (too early)
    MockTimeState::setTime(0);
    
    mockNtpSyncSuccess = false;
    
    // Sync should fail
    TEST_ASSERT_FALSE(manager.syncTime());
    TEST_ASSERT_FALSE(manager.isTimeSynced());
}

void test_ntp_sync_required_for_schedule() {
    ScheduleManager manager;
    manager.begin(12, 0, 0);
    
    // Without sync, should not be able to check upload time
    TEST_ASSERT_FALSE(manager.isUploadTime());
    TEST_ASSERT_EQUAL(0, manager.getSecondsUntilNextUpload());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Next upload time calculation tests
    RUN_TEST(test_next_upload_time_before_upload_hour);
    RUN_TEST(test_next_upload_time_after_upload_hour);
    RUN_TEST(test_next_upload_time_at_upload_hour);
    RUN_TEST(test_next_upload_time_different_hours);
    
    // Upload time detection tests
    RUN_TEST(test_is_upload_time_before_hour);
    RUN_TEST(test_is_upload_time_during_hour);
    RUN_TEST(test_is_upload_time_after_hour);
    RUN_TEST(test_is_upload_time_without_sync);
    
    // Schedule window logic tests
    RUN_TEST(test_schedule_window_current_day);
    RUN_TEST(test_schedule_window_next_day);
    RUN_TEST(test_schedule_window_midnight_crossing);
    
    // Timestamp tracking tests
    RUN_TEST(test_timestamp_tracking_initial);
    RUN_TEST(test_timestamp_tracking_after_mark);
    RUN_TEST(test_timestamp_prevents_duplicate_upload);
    RUN_TEST(test_timestamp_allows_next_day_upload);
    RUN_TEST(test_timestamp_set_manually);
    
    // Timezone offset tests
    RUN_TEST(test_timezone_offset_positive);
    RUN_TEST(test_timezone_offset_negative);
    RUN_TEST(test_timezone_offset_with_daylight_saving);
    RUN_TEST(test_timezone_offset_calculation);
    
    // NTP sync tests
    RUN_TEST(test_ntp_sync_success);
    RUN_TEST(test_ntp_sync_failure);
    RUN_TEST(test_ntp_sync_required_for_schedule);
    
    return UNITY_END();
}
