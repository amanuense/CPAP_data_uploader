#include <unity.h>
#include "Arduino.h"
#include "FS.h"
#include "MockTime.h"

void setUp(void) {
    // Reset time before each test
    MockTimeState::reset();
}

void tearDown(void) {
    // Cleanup after each test
}

// Test MockTime functionality
void test_mock_time_millis() {
    MockTimeState::setMillis(1000);
    TEST_ASSERT_EQUAL(1000, millis());
    
    MockTimeState::advanceMillis(500);
    TEST_ASSERT_EQUAL(1500, millis());
}

void test_mock_time_seconds() {
    MockTimeState::setTime(1699876800);  // Some timestamp
    TEST_ASSERT_EQUAL(1699876800, time(nullptr));
    
    MockTimeState::advanceTime(3600);  // Advance 1 hour
    TEST_ASSERT_EQUAL(1699880400, time(nullptr));
}

void test_mock_time_delay() {
    MockTimeState::setMillis(0);
    delay(100);
    TEST_ASSERT_EQUAL(100, millis());
}

// Test MockFS functionality
void test_mock_fs_add_file() {
    fs::FS mockSD;
    
    mockSD.addFile("/test.txt", "Hello, World!");
    TEST_ASSERT_TRUE(mockSD.exists("/test.txt"));
}

void test_mock_fs_read_file() {
    fs::FS mockSD;
    
    mockSD.addFile("/test.txt", "Hello, World!");
    
    fs::File file = mockSD.open("/test.txt", "r");
    TEST_ASSERT_TRUE(file);
    TEST_ASSERT_EQUAL(13, file.size());
    
    uint8_t buffer[20];
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(13, bytesRead);
    
    buffer[bytesRead] = '\0';
    TEST_ASSERT_EQUAL_STRING("Hello, World!", (char*)buffer);
    
    file.close();
}

void test_mock_fs_write_file() {
    fs::FS mockSD;
    
    fs::File file = mockSD.open("/output.txt", "w");
    TEST_ASSERT_TRUE(file);
    
    const char* text = "Test output";
    file.write((const uint8_t*)text, strlen(text));
    file.close();
    
    // Verify the file was written
    TEST_ASSERT_TRUE(mockSD.exists("/output.txt"));
    
    std::vector<uint8_t> content = mockSD.getFileContent("/output.txt");
    TEST_ASSERT_EQUAL(11, content.size());
    
    std::string result(content.begin(), content.end());
    TEST_ASSERT_EQUAL_STRING("Test output", result.c_str());
}

void test_mock_fs_directory() {
    fs::FS mockSD;
    
    mockSD.addDirectory("/DATALOG");
    TEST_ASSERT_TRUE(mockSD.exists("/DATALOG"));
    
    mockSD.addFile("/DATALOG/file1.edf", "data1");
    mockSD.addFile("/DATALOG/file2.edf", "data2");
    
    std::vector<String> files = mockSD.listDir("/DATALOG");
    TEST_ASSERT_EQUAL(2, files.size());
}

// Test String mock functionality
void test_mock_string_operations() {
    String str1 = "Hello";
    String str2 = "World";
    
    String combined = str1 + " " + str2;
    TEST_ASSERT_EQUAL_STRING("Hello World", combined.c_str());
    
    TEST_ASSERT_TRUE(combined.startsWith("Hello"));
    TEST_ASSERT_TRUE(combined.endsWith("World"));
    
    TEST_ASSERT_EQUAL(11, combined.length());
}

void test_mock_string_substring() {
    String str = "Hello World";
    
    String sub = str.substring(0, 5);
    TEST_ASSERT_EQUAL_STRING("Hello", sub.c_str());
    
    String sub2 = str.substring(6);
    TEST_ASSERT_EQUAL_STRING("World", sub2.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // MockTime tests
    RUN_TEST(test_mock_time_millis);
    RUN_TEST(test_mock_time_seconds);
    RUN_TEST(test_mock_time_delay);
    
    // MockFS tests
    RUN_TEST(test_mock_fs_add_file);
    RUN_TEST(test_mock_fs_read_file);
    RUN_TEST(test_mock_fs_write_file);
    RUN_TEST(test_mock_fs_directory);
    
    // String tests
    RUN_TEST(test_mock_string_operations);
    RUN_TEST(test_mock_string_substring);
    
    return UNITY_END();
}
