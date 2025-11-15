#include <unity.h>
#include "Arduino.h"
#include "MockTime.h"

// Include mock implementations
#include "../mocks/Arduino.cpp"

// Include the TimeBudgetManager implementation
#include "TimeBudgetManager.h"
#include "../../src/TimeBudgetManager.cpp"

void setUp(void) {
    // Reset time before each test
    MockTimeState::reset();
}

void tearDown(void) {
    // Cleanup after each test
}

// Test session initialization with different durations
void test_session_initialization_basic() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(1000);
    manager.startSession(5);  // 5 seconds
    
    // Should have full budget available
    TEST_ASSERT_EQUAL(5000, manager.getRemainingBudgetMs());
    TEST_ASSERT_TRUE(manager.hasBudget());
}

void test_session_initialization_different_durations() {
    TimeBudgetManager manager;
    
    // Test 10 second session
    MockTimeState::setMillis(0);
    manager.startSession(10);
    TEST_ASSERT_EQUAL(10000, manager.getRemainingBudgetMs());
    
    // Test 30 second session
    MockTimeState::setMillis(5000);
    manager.startSession(30);
    TEST_ASSERT_EQUAL(30000, manager.getRemainingBudgetMs());
    
    // Test 1 second session
    MockTimeState::setMillis(10000);
    manager.startSession(1);
    TEST_ASSERT_EQUAL(1000, manager.getRemainingBudgetMs());
}

// Test budget remaining calculation
void test_budget_remaining_calculation() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(1000);
    manager.startSession(10);  // 10 seconds = 10000ms
    
    // Initially should have full budget
    TEST_ASSERT_EQUAL(10000, manager.getRemainingBudgetMs());
    
    // Advance time by 3 seconds
    MockTimeState::advanceMillis(3000);
    TEST_ASSERT_EQUAL(7000, manager.getRemainingBudgetMs());
    
    // Advance time by another 5 seconds
    MockTimeState::advanceMillis(5000);
    TEST_ASSERT_EQUAL(2000, manager.getRemainingBudgetMs());
    
    // Advance time past budget
    MockTimeState::advanceMillis(3000);
    TEST_ASSERT_EQUAL(0, manager.getRemainingBudgetMs());
}

void test_has_budget() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(5);  // 5 seconds
    
    TEST_ASSERT_TRUE(manager.hasBudget());
    
    // Advance to just before budget exhaustion
    MockTimeState::advanceMillis(4999);
    TEST_ASSERT_TRUE(manager.hasBudget());
    
    // Advance to exactly budget exhaustion
    MockTimeState::advanceMillis(1);
    TEST_ASSERT_FALSE(manager.hasBudget());
    
    // Advance past budget
    MockTimeState::advanceMillis(1000);
    TEST_ASSERT_FALSE(manager.hasBudget());
}

// Test upload time estimation with various file sizes
void test_upload_time_estimation_default_rate() {
    TimeBudgetManager manager;
    
    // Default rate is 500 KB/s = 512000 bytes/s
    unsigned long defaultRate = 500 * 1024;
    
    // Test 500 KB file (should take exactly 1 second at 500 KB/s)
    unsigned long fileSize1 = 500 * 1024;
    unsigned long estimatedTime1 = manager.estimateUploadTimeMs(fileSize1);
    TEST_ASSERT_EQUAL(1000, estimatedTime1);
    
    // Test 250 KB file (should take exactly 0.5 seconds)
    unsigned long fileSize2 = 250 * 1024;
    unsigned long estimatedTime2 = manager.estimateUploadTimeMs(fileSize2);
    TEST_ASSERT_EQUAL(500, estimatedTime2);
    
    // Test 1000 KB file (should take exactly 2 seconds)
    unsigned long fileSize3 = 1000 * 1024;
    unsigned long estimatedTime3 = manager.estimateUploadTimeMs(fileSize3);
    TEST_ASSERT_EQUAL(2000, estimatedTime3);
}

void test_upload_time_estimation_various_sizes() {
    TimeBudgetManager manager;
    
    // Test small file (10 KB)
    unsigned long smallFile = 10 * 1024;
    unsigned long estimatedSmall = manager.estimateUploadTimeMs(smallFile);
    TEST_ASSERT_GREATER_THAN(0, estimatedSmall);
    TEST_ASSERT_LESS_THAN(100, estimatedSmall);  // Should be < 100ms
    
    // Test medium file (500 KB)
    unsigned long mediumFile = 500 * 1024;
    unsigned long estimatedMedium = manager.estimateUploadTimeMs(mediumFile);
    TEST_ASSERT_GREATER_THAN(900, estimatedMedium);
    TEST_ASSERT_LESS_THAN(1100, estimatedMedium);  // Should be ~1000ms
    
    // Test large file (5 MB)
    unsigned long largeFile = 5 * 1024 * 1024;
    unsigned long estimatedLarge = manager.estimateUploadTimeMs(largeFile);
    TEST_ASSERT_GREATER_THAN(9000, estimatedLarge);
    TEST_ASSERT_LESS_THAN(11000, estimatedLarge);  // Should be ~10000ms
}

void test_can_upload_file() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(5);  // 5 seconds = 5000ms
    
    // Small file that fits in budget (256 KB, ~500ms)
    TEST_ASSERT_TRUE(manager.canUploadFile(256 * 1024));
    
    // Medium file that fits in budget (2 MB, ~4000ms)
    TEST_ASSERT_TRUE(manager.canUploadFile(2 * 1024 * 1024));
    
    // Large file that doesn't fit (10 MB, ~20000ms)
    TEST_ASSERT_FALSE(manager.canUploadFile(10 * 1024 * 1024));
    
    // Advance time and check again
    MockTimeState::advanceMillis(3000);  // 2 seconds remaining
    
    // Small file still fits
    TEST_ASSERT_TRUE(manager.canUploadFile(256 * 1024));
    
    // Medium file no longer fits
    TEST_ASSERT_FALSE(manager.canUploadFile(2 * 1024 * 1024));
}

// Test transmission rate averaging over multiple uploads
void test_transmission_rate_single_upload() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(10);
    
    // Record upload: 512 KB in 1000ms = 512 KB/s
    manager.recordUpload(512 * 1024, 1000);
    
    // Estimate should now use the recorded rate
    unsigned long fileSize = 512 * 1024;
    unsigned long estimatedTime = manager.estimateUploadTimeMs(fileSize);
    TEST_ASSERT_EQUAL(1000, estimatedTime);
}

void test_transmission_rate_averaging() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(30);
    
    // Record 5 uploads with different rates
    // Upload 1: 512 KB in 1000ms = 512 KB/s = 524288 bytes/s
    manager.recordUpload(512 * 1024, 1000);
    
    // Upload 2: 256 KB in 500ms = 512 KB/s = 524288 bytes/s
    manager.recordUpload(256 * 1024, 500);
    
    // Upload 3: 1024 KB in 2000ms = 512 KB/s = 524288 bytes/s
    manager.recordUpload(1024 * 1024, 2000);
    
    // Upload 4: 512 KB in 500ms = 1024 KB/s = 1048576 bytes/s
    manager.recordUpload(512 * 1024, 500);
    
    // Upload 5: 512 KB in 2000ms = 256 KB/s = 262144 bytes/s
    manager.recordUpload(512 * 1024, 2000);
    
    // Average should be (524288 + 524288 + 524288 + 1048576 + 262144) / 5 = 576716.8 bytes/s
    // For 576716.8 bytes/s, 512 KB should take ~909ms
    unsigned long fileSize = 512 * 1024;
    unsigned long estimatedTime = manager.estimateUploadTimeMs(fileSize);
    
    // Allow some tolerance for integer division
    TEST_ASSERT_GREATER_THAN(850, estimatedTime);
    TEST_ASSERT_LESS_THAN(950, estimatedTime);
}

void test_transmission_rate_history_limit() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(60);
    
    // Record 7 uploads (more than history size of 5)
    // First two should be dropped from average
    for (int i = 0; i < 7; i++) {
        // Each upload: 512 KB in 1000ms = 512 KB/s
        manager.recordUpload(512 * 1024, 1000);
    }
    
    // Average should still be based on last 5 uploads only
    unsigned long fileSize = 512 * 1024;
    unsigned long estimatedTime = manager.estimateUploadTimeMs(fileSize);
    TEST_ASSERT_EQUAL(1000, estimatedTime);
}

void test_transmission_rate_varying_speeds() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(30);
    
    // Record uploads with very different speeds
    // Fast upload: 512 KB in 250ms = 2048 KB/s
    manager.recordUpload(512 * 1024, 250);
    
    // Slow upload: 512 KB in 4000ms = 128 KB/s
    manager.recordUpload(512 * 1024, 4000);
    
    // Medium upload: 512 KB in 1000ms = 512 KB/s
    manager.recordUpload(512 * 1024, 1000);
    
    // Average: (2097152 + 131072 + 524288) / 3 = 917504 bytes/s
    // For 512 KB at 917504 bytes/s: ~571ms
    unsigned long fileSize = 512 * 1024;
    unsigned long estimatedTime = manager.estimateUploadTimeMs(fileSize);
    
    TEST_ASSERT_GREATER_THAN(500, estimatedTime);
    TEST_ASSERT_LESS_THAN(650, estimatedTime);
}

void test_record_upload_zero_time() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(10);
    
    // Record upload with zero elapsed time (should be ignored)
    manager.recordUpload(512 * 1024, 0);
    
    // Should still use default rate (500 KB/s = 512000 bytes/s)
    // 500 KB file should take 1000ms at default rate
    unsigned long fileSize = 500 * 1024;
    unsigned long estimatedTime = manager.estimateUploadTimeMs(fileSize);
    TEST_ASSERT_EQUAL(1000, estimatedTime);  // Default 500 KB/s
}

// Test retry multiplier application
void test_retry_multiplier_basic() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(5, 1);  // 5 seconds, multiplier 1
    TEST_ASSERT_EQUAL(5000, manager.getRemainingBudgetMs());
    
    MockTimeState::setMillis(1000);
    manager.startSession(5, 2);  // 5 seconds, multiplier 2
    TEST_ASSERT_EQUAL(10000, manager.getRemainingBudgetMs());
    
    MockTimeState::setMillis(2000);
    manager.startSession(5, 3);  // 5 seconds, multiplier 3
    TEST_ASSERT_EQUAL(15000, manager.getRemainingBudgetMs());
}

void test_retry_multiplier_various_values() {
    TimeBudgetManager manager;
    
    // Test multiplier 4
    MockTimeState::setMillis(0);
    manager.startSession(10, 4);
    TEST_ASSERT_EQUAL(40000, manager.getRemainingBudgetMs());
    
    // Test multiplier 5
    MockTimeState::setMillis(1000);
    manager.startSession(10, 5);
    TEST_ASSERT_EQUAL(50000, manager.getRemainingBudgetMs());
    
    // Test multiplier 10
    MockTimeState::setMillis(2000);
    manager.startSession(10, 10);
    TEST_ASSERT_EQUAL(100000, manager.getRemainingBudgetMs());
}

void test_retry_multiplier_with_time_progression() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(5, 3);  // 5 seconds * 3 = 15 seconds
    
    TEST_ASSERT_EQUAL(15000, manager.getRemainingBudgetMs());
    TEST_ASSERT_TRUE(manager.hasBudget());
    
    // Advance time
    MockTimeState::advanceMillis(10000);  // 10 seconds elapsed
    TEST_ASSERT_EQUAL(5000, manager.getRemainingBudgetMs());
    TEST_ASSERT_TRUE(manager.hasBudget());
    
    // Advance past budget
    MockTimeState::advanceMillis(6000);  // 16 seconds total
    TEST_ASSERT_EQUAL(0, manager.getRemainingBudgetMs());
    TEST_ASSERT_FALSE(manager.hasBudget());
}

// Test wait time calculation (2x session duration)
void test_wait_time_calculation() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(0);
    manager.startSession(5);  // 5 seconds
    
    // Wait time should be 2x session duration = 10 seconds = 10000ms
    TEST_ASSERT_EQUAL(10000, manager.getWaitTimeMs());
}

void test_wait_time_various_durations() {
    TimeBudgetManager manager;
    
    // Test 10 second session -> 20 second wait
    MockTimeState::setMillis(0);
    manager.startSession(10);
    TEST_ASSERT_EQUAL(20000, manager.getWaitTimeMs());
    
    // Test 30 second session -> 60 second wait
    MockTimeState::setMillis(1000);
    manager.startSession(30);
    TEST_ASSERT_EQUAL(60000, manager.getWaitTimeMs());
    
    // Test 1 second session -> 2 second wait
    MockTimeState::setMillis(2000);
    manager.startSession(1);
    TEST_ASSERT_EQUAL(2000, manager.getWaitTimeMs());
}

void test_wait_time_with_retry_multiplier() {
    TimeBudgetManager manager;
    
    // Session with retry multiplier
    MockTimeState::setMillis(0);
    manager.startSession(5, 3);  // 5 seconds * 3 = 15 seconds
    
    // Wait time should be 2x the multiplied duration = 30 seconds
    TEST_ASSERT_EQUAL(30000, manager.getWaitTimeMs());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Session initialization tests
    RUN_TEST(test_session_initialization_basic);
    RUN_TEST(test_session_initialization_different_durations);
    
    // Budget remaining tests
    RUN_TEST(test_budget_remaining_calculation);
    RUN_TEST(test_has_budget);
    
    // Upload time estimation tests
    RUN_TEST(test_upload_time_estimation_default_rate);
    RUN_TEST(test_upload_time_estimation_various_sizes);
    RUN_TEST(test_can_upload_file);
    
    // Transmission rate averaging tests
    RUN_TEST(test_transmission_rate_single_upload);
    RUN_TEST(test_transmission_rate_averaging);
    RUN_TEST(test_transmission_rate_history_limit);
    RUN_TEST(test_transmission_rate_varying_speeds);
    RUN_TEST(test_record_upload_zero_time);
    
    // Retry multiplier tests
    RUN_TEST(test_retry_multiplier_basic);
    RUN_TEST(test_retry_multiplier_various_values);
    RUN_TEST(test_retry_multiplier_with_time_progression);
    
    // Wait time calculation tests
    RUN_TEST(test_wait_time_calculation);
    RUN_TEST(test_wait_time_various_durations);
    RUN_TEST(test_wait_time_with_retry_multiplier);
    
    return UNITY_END();
}
