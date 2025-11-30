#include <unity.h>
#include <Arduino.h>

// Mock dependencies
#include "../mocks/MockFS.h"
#include "../mocks/MockLogger.h"

// Mock File class that can simulate access failures
class FailingMockFile {
private:
    bool shouldFail;
    bool isDir;
    String filePath;
    
public:
    FailingMockFile() : shouldFail(false), isDir(false) {}
    
    FailingMockFile(bool fail, bool directory = false, const String& path = "") 
        : shouldFail(fail), isDir(directory), filePath(path) {}
    
    operator bool() const {
        return !shouldFail;  // Returns false if should fail
    }
    
    bool isDirectory() const {
        return isDir;
    }
    
    String name() const {
        return filePath;
    }
    
    void close() {}
    
    FailingMockFile openNextFile() {
        // Return invalid file to simulate end of directory
        return FailingMockFile(true);
    }
};

// Mock FS that can simulate CPAP interference
class FailingMockFS {
private:
    bool simulateCPAPUsing;
    std::map<std::string, bool> folderAccessible;
    std::map<std::string, std::vector<String>> folderContents;
    
public:
    FailingMockFS() : simulateCPAPUsing(false) {}
    
    void setCPAPUsing(bool using_sd) {
        simulateCPAPUsing = using_sd;
    }
    
    void setFolderAccessible(const String& path, bool accessible) {
        folderAccessible[path.toStdString()] = accessible;
    }
    
    void addFolderWithFiles(const String& path, const std::vector<String>& files) {
        folderContents[path.toStdString()] = files;
        folderAccessible[path.toStdString()] = true;
    }
    
    bool exists(const String& path) {
        if (simulateCPAPUsing) {
            return false;  // Simulate SD card not accessible
        }
        return folderAccessible.find(path.toStdString()) != folderAccessible.end();
    }
    
    FailingMockFile open(const String& path) {
        // Check if CPAP is using SD card
        if (simulateCPAPUsing) {
            return FailingMockFile(true);  // Fail to open
        }
        
        // Check if folder is marked as accessible
        auto it = folderAccessible.find(path.toStdString());
        if (it != folderAccessible.end() && !it->second) {
            return FailingMockFile(true);  // Fail to open
        }
        
        // Check if folder exists
        if (folderContents.find(path.toStdString()) != folderContents.end()) {
            return FailingMockFile(false, true, path);  // Success, is directory
        }
        
        return FailingMockFile(true);  // Fail to open
    }
    
    std::vector<String> getFolderFiles(const String& path) {
        auto it = folderContents.find(path.toStdString());
        if (it != folderContents.end()) {
            return it->second;
        }
        return std::vector<String>();
    }
};

// Mock UploadStateManager
class MockUploadStateManager {
private:
    std::map<std::string, bool> completedFolders;
    String currentRetryFolder;
    int retryCount;
    
public:
    MockUploadStateManager() : retryCount(0) {}
    
    bool isFolderCompleted(const String& folder) {
        return completedFolders[folder.toStdString()];
    }
    
    void markFolderCompleted(const String& folder) {
        completedFolders[folder.toStdString()] = true;
    }
    
    void setCurrentRetryFolder(const String& folder) {
        currentRetryFolder = folder;
    }
    
    String getCurrentRetryFolder() const {
        return currentRetryFolder;
    }
    
    int getCurrentRetryCount() const {
        return retryCount;
    }
    
    void incrementCurrentRetryCount() {
        retryCount++;
    }
    
    void clearCurrentRetry() {
        currentRetryFolder = String("");
        retryCount = 0;
    }
    
    bool save(FailingMockFS& sd) {
        return true;
    }
    
    // Test helper
    bool wasFolderMarkedComplete(const String& folder) {
        return completedFolders[folder.toStdString()];
    }
    
    void reset() {
        completedFolders.clear();
        currentRetryFolder = String("");
        retryCount = 0;
    }
};

// Simplified FileUploader for testing scan failure scenarios
class FileUploaderScanTest {
private:
    FailingMockFS* mockFS;
    MockUploadStateManager* stateManager;
    
public:
    FileUploaderScanTest(FailingMockFS* fs, MockUploadStateManager* state)
        : mockFS(fs), stateManager(state) {}
    
    // Simulate scanFolderFiles with error handling
    std::vector<String> scanFolderFiles(const String& folderPath) {
        std::vector<String> files;
        
        FailingMockFile folder = mockFS->open(folderPath);
        if (!folder) {
            // LOG_ERROR: Failed to open folder
            return files;  // Return empty - indicates scan failure
        }
        
        if (!folder.isDirectory()) {
            // LOG_ERROR: Path is not a directory
            folder.close();
            return files;
        }
        
        // Get files from mock
        files = mockFS->getFolderFiles(folderPath);
        
        folder.close();
        return files;
    }
    
    // Simulate uploadDatalogFolder with proper error handling
    bool uploadDatalogFolder(const String& folderName) {
        String folderPath = String("/DATALOG/") + folderName;
        
        // Verify folder exists before scanning
        FailingMockFile folderCheck = mockFS->open(folderPath);
        if (!folderCheck) {
            // LOG_ERROR: Cannot access folder
            stateManager->incrementCurrentRetryCount();
            stateManager->save(*mockFS);
            return false;  // Treat as error, not completion
        }
        
        if (!folderCheck.isDirectory()) {
            // LOG_ERROR: Path is not a directory
            folderCheck.close();
            stateManager->incrementCurrentRetryCount();
            stateManager->save(*mockFS);
            return false;
        }
        folderCheck.close();
        
        // Scan for files in the folder
        std::vector<String> files = scanFolderFiles(folderPath);
        
        if (files.empty()) {
            // Need to distinguish between "truly empty" and "scan failed"
            FailingMockFile verifyFolder = mockFS->open(folderPath);
            if (!verifyFolder) {
                // LOG_ERROR: Folder scan returned empty but folder is not accessible
                stateManager->incrementCurrentRetryCount();
                stateManager->save(*mockFS);
                return false;  // Treat as error
            }
            verifyFolder.close();
            
            // Folder is accessible but truly empty
            stateManager->markFolderCompleted(folderName);
            stateManager->clearCurrentRetry();
            return true;
        }
        
        // Simulate successful upload
        stateManager->markFolderCompleted(folderName);
        stateManager->clearCurrentRetry();
        return true;
    }
};

// Test fixtures
FailingMockFS testFS;
MockUploadStateManager testStateManager;
FileUploaderScanTest* uploader = nullptr;

void setUp(void) {
    testFS = FailingMockFS();
    testStateManager.reset();
    uploader = new FileUploaderScanTest(&testFS, &testStateManager);
}

void tearDown(void) {
    if (uploader) {
        delete uploader;
        uploader = nullptr;
    }
}

// Test: Folder with files is scanned and marked complete
void test_normal_folder_scan_success(void) {
    std::vector<String> files = {String("file1.edf"), String("file2.edf")};
    testFS.addFolderWithFiles(String("/DATALOG/20241130"), files);
    
    bool result = uploader->uploadDatalogFolder(String("20241130"));
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    TEST_ASSERT_EQUAL(0, testStateManager.getCurrentRetryCount());
}

// Test: Empty folder is marked complete (not an error)
void test_empty_folder_marked_complete(void) {
    std::vector<String> files;  // Empty
    testFS.addFolderWithFiles(String("/DATALOG/20241130"), files);
    
    bool result = uploader->uploadDatalogFolder(String("20241130"));
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    TEST_ASSERT_EQUAL(0, testStateManager.getCurrentRetryCount());
}

// Test: CRITICAL - Folder access failure does NOT mark as complete
void test_folder_access_failure_not_marked_complete(void) {
    // Simulate CPAP using SD card
    testFS.setCPAPUsing(true);
    
    bool result = uploader->uploadDatalogFolder(String("20241130"));
    
    // Should fail
    TEST_ASSERT_FALSE(result);
    
    // CRITICAL: Should NOT be marked as complete
    TEST_ASSERT_FALSE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    
    // Should increment retry count
    TEST_ASSERT_EQUAL(1, testStateManager.getCurrentRetryCount());
}

// Test: CRITICAL - Scan returns empty but folder inaccessible
void test_scan_empty_but_folder_inaccessible(void) {
    // Add folder but make it inaccessible
    std::vector<String> files = {String("file1.edf")};
    testFS.addFolderWithFiles(String("/DATALOG/20241130"), files);
    testFS.setFolderAccessible(String("/DATALOG/20241130"), false);
    
    bool result = uploader->uploadDatalogFolder(String("20241130"));
    
    // Should fail
    TEST_ASSERT_FALSE(result);
    
    // CRITICAL: Should NOT be marked as complete
    TEST_ASSERT_FALSE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    
    // Should increment retry count
    TEST_ASSERT_EQUAL(1, testStateManager.getCurrentRetryCount());
}

// Test: Folder becomes accessible after CPAP releases SD card
void test_folder_accessible_after_cpap_release(void) {
    std::vector<String> files = {String("file1.edf")};
    testFS.addFolderWithFiles(String("/DATALOG/20241130"), files);
    
    // First attempt: CPAP using SD card
    testFS.setCPAPUsing(true);
    bool result1 = uploader->uploadDatalogFolder(String("20241130"));
    
    TEST_ASSERT_FALSE(result1);
    TEST_ASSERT_FALSE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    TEST_ASSERT_EQUAL(1, testStateManager.getCurrentRetryCount());
    
    // Second attempt: CPAP released SD card
    testFS.setCPAPUsing(false);
    bool result2 = uploader->uploadDatalogFolder(String("20241130"));
    
    TEST_ASSERT_TRUE(result2);
    TEST_ASSERT_TRUE(testStateManager.wasFolderMarkedComplete(String("20241130")));
    TEST_ASSERT_EQUAL(0, testStateManager.getCurrentRetryCount());  // Cleared after success
}

// Test: Multiple retry attempts increment counter
void test_multiple_retry_attempts(void) {
    testFS.setCPAPUsing(true);
    
    // Attempt 1
    uploader->uploadDatalogFolder(String("20241130"));
    TEST_ASSERT_EQUAL(1, testStateManager.getCurrentRetryCount());
    
    // Attempt 2
    uploader->uploadDatalogFolder(String("20241130"));
    TEST_ASSERT_EQUAL(2, testStateManager.getCurrentRetryCount());
    
    // Attempt 3
    uploader->uploadDatalogFolder(String("20241130"));
    TEST_ASSERT_EQUAL(3, testStateManager.getCurrentRetryCount());
    
    // Folder should never be marked complete
    TEST_ASSERT_FALSE(testStateManager.wasFolderMarkedComplete(String("20241130")));
}

// Test: Scan failure is distinguishable from empty folder
void test_distinguish_scan_failure_from_empty(void) {
    // Test 1: Truly empty folder (accessible)
    std::vector<String> emptyFiles;
    testFS.addFolderWithFiles(String("/DATALOG/20241201"), emptyFiles);
    
    bool result1 = uploader->uploadDatalogFolder(String("20241201"));
    TEST_ASSERT_TRUE(result1);
    TEST_ASSERT_TRUE(testStateManager.wasFolderMarkedComplete(String("20241201")));
    
    // Test 2: Scan failure (inaccessible)
    testFS.setFolderAccessible(String("/DATALOG/20241202"), false);
    
    bool result2 = uploader->uploadDatalogFolder(String("20241202"));
    TEST_ASSERT_FALSE(result2);
    TEST_ASSERT_FALSE(testStateManager.wasFolderMarkedComplete(String("20241202")));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_normal_folder_scan_success);
    RUN_TEST(test_empty_folder_marked_complete);
    RUN_TEST(test_folder_access_failure_not_marked_complete);
    RUN_TEST(test_scan_empty_but_folder_inaccessible);
    RUN_TEST(test_folder_accessible_after_cpap_release);
    RUN_TEST(test_multiple_retry_attempts);
    RUN_TEST(test_distinguish_scan_failure_from_empty);
    
    return UNITY_END();
}
