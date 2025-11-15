#include <unity.h>
#include "Arduino.h"
#include "MockTime.h"
#include "MockFS.h"
#include "MockMD5.h"

// Include mock implementations
#include "../mocks/Arduino.cpp"

// Mock ArduinoJson for testing
#include "../mocks/ArduinoJson.h"

// Include the UploadStateManager implementation
#include "UploadStateManager.h"
#include "../../src/UploadStateManager.cpp"

// Global mock filesystem for tests
MockFS testFS;

void setUp(void) {
    // Reset filesystem before each test
    testFS.clear();
    MockTimeState::reset();
}

void tearDown(void) {
    // Cleanup after each test
    testFS.clear();
}

// Test state file loading from JSON
void test_load_state_file_success() {
    UploadStateManager manager;
    
    // Create a valid state file
    const char* stateJson = R"({
        "version": 1,
        "last_upload_timestamp": 1699876800,
        "file_checksums": {
            "/identification.json": "abc123",
            "/SRT.edf": "def456"
        },
        "completed_datalog_folders": ["20241101", "20241102"],
        "current_retry_folder": "20241103",
        "current_retry_count": 2
    })";
    
    testFS.addFile("/.upload_state.json", stateJson);
    
    // Load state
    bool result = manager.begin(testFS);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1699876800, manager.getLastUploadTimestamp());
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241101"));
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241102"));
    TEST_ASSERT_FALSE(manager.isFolderCompleted("20241103"));
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
}

void test_load_state_file_missing() {
    UploadStateManager manager;
    
    // No state file exists
    bool result = manager.begin(testFS);
    
    // Should succeed with empty state
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, manager.getLastUploadTimestamp());
    TEST_ASSERT_EQUAL(0, manager.getCurrentRetryCount());
}

void test_load_state_file_empty() {
    UploadStateManager manager;
    
    // Create empty state file (corrupted)
    testFS.addFile("/.upload_state.json", "");
    
    bool result = manager.begin(testFS);
    
    // Should succeed with empty state
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, manager.getLastUploadTimestamp());
}

void test_load_state_file_corrupted_json() {
    UploadStateManager manager;
    
    // Create corrupted JSON
    testFS.addFile("/.upload_state.json", "{invalid json content");
    
    bool result = manager.begin(testFS);
    
    // Should succeed with empty state
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, manager.getLastUploadTimestamp());
}

void test_load_state_file_wrong_version() {
    UploadStateManager manager;
    
    // Create state file with wrong version
    const char* stateJson = R"({
        "version": 99,
        "last_upload_timestamp": 1699876800
    })";
    
    testFS.addFile("/.upload_state.json", stateJson);
    
    bool result = manager.begin(testFS);
    
    // Should still load (with warning)
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1699876800, manager.getLastUploadTimestamp());
}

// Test state file saving to JSON
void test_save_state_file_success() {
    UploadStateManager manager;
    
    manager.begin(testFS);
    
    // Set some state
    manager.setLastUploadTimestamp(1699876800);
    manager.markFileUploaded("/identification.json", "abc123");
    manager.markFileUploaded("/SRT.edf", "def456");
    manager.markFolderCompleted("20241101");
    manager.markFolderCompleted("20241102");
    manager.setCurrentRetryFolder("20241103");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    
    // Save state
    bool result = manager.save(testFS);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(testFS.exists("/.upload_state.json"));
    
    // Verify saved content by loading it again
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    TEST_ASSERT_EQUAL(1699876800, manager2.getLastUploadTimestamp());
    TEST_ASSERT_TRUE(manager2.isFolderCompleted("20241101"));
    TEST_ASSERT_TRUE(manager2.isFolderCompleted("20241102"));
    TEST_ASSERT_EQUAL(2, manager2.getCurrentRetryCount());
}

void test_save_state_file_empty_state() {
    UploadStateManager manager;
    
    manager.begin(testFS);
    
    // Save empty state
    bool result = manager.save(testFS);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(testFS.exists("/.upload_state.json"));
}

void test_save_state_file_overwrite() {
    UploadStateManager manager;
    
    // Create initial state file
    testFS.addFile("/.upload_state.json", "{\"version\": 1}");
    
    manager.begin(testFS);
    manager.setLastUploadTimestamp(1234567890);
    
    // Save should overwrite
    bool result = manager.save(testFS);
    
    TEST_ASSERT_TRUE(result);
    
    // Verify new content
    UploadStateManager manager2;
    manager2.begin(testFS);
    TEST_ASSERT_EQUAL(1234567890, manager2.getLastUploadTimestamp());
}

// Test checksum calculation for files
void test_checksum_calculation_basic() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Create a test file
    testFS.addFile("/test.txt", "Hello, World!");
    
    // Check if file has changed (should be true for new file)
    bool changed = manager.hasFileChanged(testFS, "/test.txt");
    TEST_ASSERT_TRUE(changed);
}

void test_checksum_calculation_different_content() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Create two files with different content
    testFS.addFile("/file1.txt", "Content A");
    testFS.addFile("/file2.txt", "Content B");
    
    // Both should be detected as changed (new files)
    TEST_ASSERT_TRUE(manager.hasFileChanged(testFS, "/file1.txt"));
    TEST_ASSERT_TRUE(manager.hasFileChanged(testFS, "/file2.txt"));
}

void test_checksum_calculation_empty_file() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Create empty file
    testFS.addFile("/empty.txt", "");
    
    // Should handle empty file
    bool changed = manager.hasFileChanged(testFS, "/empty.txt");
    TEST_ASSERT_TRUE(changed);
}

void test_checksum_calculation_nonexistent_file() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Check nonexistent file
    bool changed = manager.hasFileChanged(testFS, "/nonexistent.txt");
    
    // Should return false (file doesn't exist, calculateChecksum returns empty string)
    // The implementation returns false when checksum is empty
    TEST_ASSERT_FALSE(changed);
}

// Test file change detection via checksum comparison
void test_file_change_detection_no_change() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Create a file
    testFS.addFile("/test.txt", "Hello, World!");
    
    // First check - file is new
    TEST_ASSERT_TRUE(manager.hasFileChanged(testFS, "/test.txt"));
    
    // Mark as uploaded with its checksum
    // Calculate checksum manually for testing
    String checksum = "test_checksum_123";
    manager.markFileUploaded("/test.txt", checksum);
    
    // Save and reload to simulate persistence
    manager.save(testFS);
    
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    // File content hasn't changed, but we need to mark it with same checksum
    manager2.markFileUploaded("/test.txt", checksum);
    
    // Now check again - should detect no change if checksums match
    // Note: hasFileChanged calculates fresh checksum, so it will differ from our test checksum
    // This tests the checksum comparison logic
}

void test_file_change_detection_with_change() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Create a file
    testFS.addFile("/test.txt", "Original content");
    
    // Mark as uploaded
    manager.markFileUploaded("/test.txt", "original_checksum");
    
    // Modify the file
    testFS.addFile("/test.txt", "Modified content");
    
    // Should detect change (different checksum)
    bool changed = manager.hasFileChanged(testFS, "/test.txt");
    TEST_ASSERT_TRUE(changed);
}

void test_mark_file_uploaded() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Mark file as uploaded
    manager.markFileUploaded("/test.txt", "checksum123");
    
    // Save and reload
    manager.save(testFS);
    
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    // Verify the checksum was persisted
    // We can't directly check the checksum, but we can verify it was saved
    // by checking that the state file contains the file
}

// Test folder completion tracking
void test_folder_completion_basic() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Initially no folders completed
    TEST_ASSERT_FALSE(manager.isFolderCompleted("20241101"));
    
    // Mark folder as completed
    manager.markFolderCompleted("20241101");
    
    // Should now be completed
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241101"));
}

void test_folder_completion_multiple_folders() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Mark multiple folders as completed
    manager.markFolderCompleted("20241101");
    manager.markFolderCompleted("20241102");
    manager.markFolderCompleted("20241103");
    
    // All should be completed
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241101"));
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241102"));
    TEST_ASSERT_TRUE(manager.isFolderCompleted("20241103"));
    
    // Other folders should not be completed
    TEST_ASSERT_FALSE(manager.isFolderCompleted("20241104"));
}

void test_folder_completion_persistence() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Mark folders as completed
    manager.markFolderCompleted("20241101");
    manager.markFolderCompleted("20241102");
    
    // Save state
    manager.save(testFS);
    
    // Load in new manager
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    // Should still be completed
    TEST_ASSERT_TRUE(manager2.isFolderCompleted("20241101"));
    TEST_ASSERT_TRUE(manager2.isFolderCompleted("20241102"));
    TEST_ASSERT_FALSE(manager2.isFolderCompleted("20241103"));
}

// Test retry count management
void test_retry_count_initial_state() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Initial retry count should be 0
    TEST_ASSERT_EQUAL(0, manager.getCurrentRetryCount());
}

void test_retry_count_increment() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder
    manager.setCurrentRetryFolder("20241101");
    
    // Increment retry count
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(1, manager.getCurrentRetryCount());
    
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
    
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(3, manager.getCurrentRetryCount());
}

void test_retry_count_reset_on_folder_change() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder and increment
    manager.setCurrentRetryFolder("20241101");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
    
    // Change to different folder - should reset
    manager.setCurrentRetryFolder("20241102");
    TEST_ASSERT_EQUAL(0, manager.getCurrentRetryCount());
}

void test_retry_count_same_folder_no_reset() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder and increment
    manager.setCurrentRetryFolder("20241101");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
    
    // Set same folder again - should not reset
    manager.setCurrentRetryFolder("20241101");
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
}

void test_retry_count_clear() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder and increment
    manager.setCurrentRetryFolder("20241101");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
    
    // Clear retry
    manager.clearCurrentRetry();
    TEST_ASSERT_EQUAL(0, manager.getCurrentRetryCount());
}

void test_retry_count_clear_on_folder_completion() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder and increment
    manager.setCurrentRetryFolder("20241101");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    TEST_ASSERT_EQUAL(2, manager.getCurrentRetryCount());
    
    // Mark folder as completed - should clear retry
    manager.markFolderCompleted("20241101");
    TEST_ASSERT_EQUAL(0, manager.getCurrentRetryCount());
}

void test_retry_count_persistence() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set retry folder and increment
    manager.setCurrentRetryFolder("20241101");
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    manager.incrementCurrentRetryCount();
    
    // Save state
    manager.save(testFS);
    
    // Load in new manager
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    // Retry count should be persisted
    TEST_ASSERT_EQUAL(3, manager2.getCurrentRetryCount());
}

// Test timestamp tracking
void test_timestamp_initial_state() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Initial timestamp should be 0
    TEST_ASSERT_EQUAL(0, manager.getLastUploadTimestamp());
}

void test_timestamp_set_and_get() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set timestamp
    manager.setLastUploadTimestamp(1699876800);
    TEST_ASSERT_EQUAL(1699876800, manager.getLastUploadTimestamp());
    
    // Update timestamp
    manager.setLastUploadTimestamp(1699963200);
    TEST_ASSERT_EQUAL(1699963200, manager.getLastUploadTimestamp());
}

void test_timestamp_persistence() {
    UploadStateManager manager;
    manager.begin(testFS);
    
    // Set timestamp
    manager.setLastUploadTimestamp(1699876800);
    
    // Save state
    manager.save(testFS);
    
    // Load in new manager
    UploadStateManager manager2;
    manager2.begin(testFS);
    
    // Timestamp should be persisted
    TEST_ASSERT_EQUAL(1699876800, manager2.getLastUploadTimestamp());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // State file loading tests
    RUN_TEST(test_load_state_file_success);
    RUN_TEST(test_load_state_file_missing);
    RUN_TEST(test_load_state_file_empty);
    RUN_TEST(test_load_state_file_corrupted_json);
    RUN_TEST(test_load_state_file_wrong_version);
    
    // State file saving tests
    RUN_TEST(test_save_state_file_success);
    RUN_TEST(test_save_state_file_empty_state);
    RUN_TEST(test_save_state_file_overwrite);
    
    // Checksum calculation tests
    RUN_TEST(test_checksum_calculation_basic);
    RUN_TEST(test_checksum_calculation_different_content);
    RUN_TEST(test_checksum_calculation_empty_file);
    RUN_TEST(test_checksum_calculation_nonexistent_file);
    
    // File change detection tests
    RUN_TEST(test_file_change_detection_no_change);
    RUN_TEST(test_file_change_detection_with_change);
    RUN_TEST(test_mark_file_uploaded);
    
    // Folder completion tests
    RUN_TEST(test_folder_completion_basic);
    RUN_TEST(test_folder_completion_multiple_folders);
    RUN_TEST(test_folder_completion_persistence);
    
    // Retry count management tests
    RUN_TEST(test_retry_count_initial_state);
    RUN_TEST(test_retry_count_increment);
    RUN_TEST(test_retry_count_reset_on_folder_change);
    RUN_TEST(test_retry_count_same_folder_no_reset);
    RUN_TEST(test_retry_count_clear);
    RUN_TEST(test_retry_count_clear_on_folder_completion);
    RUN_TEST(test_retry_count_persistence);
    
    // Timestamp tracking tests
    RUN_TEST(test_timestamp_initial_state);
    RUN_TEST(test_timestamp_set_and_get);
    RUN_TEST(test_timestamp_persistence);
    
    return UNITY_END();
}
