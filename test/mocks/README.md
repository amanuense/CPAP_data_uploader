# Mock Infrastructure

This directory contains mock implementations of hardware-dependent components for unit testing.

## Available Mocks

### ArduinoJson.h
Mock implementation of the ArduinoJson library for JSON parsing:
- `StaticJsonDocument<SIZE>` class: Mock JSON document container
- `DeserializationError` class: Mock error handling
- `deserializeJson()` function: Parses JSON from files
- Supports string, int, and long value types
- Implements pipe operator (`|`) for default values

Key features:
- Parse JSON from MockFile objects
- Access values with `doc["key"]` syntax
- Provide default values with `doc["key"] | defaultValue`
- Handles nested objects (basic support)

### Arduino.h
Mock implementation of Arduino core functions and types:
- Basic types: `byte`, `boolean`
- Time functions: `millis()`, `micros()`, `delay()`
- Math functions: `min()`, `max()`, `constrain()`, `map()`
- Serial communication: `Serial` object for logging
- Pin functions: `pinMode()`, `digitalWrite()`, `digitalRead()` (no-ops)

### MockFS.h
Mock filesystem implementation that simulates SD card operations:
- `MockFS` class: Simulates the `fs::FS` interface
- `MockFile` class: Simulates file operations
- `String` class: Mock Arduino String class
- In-memory file storage for testing

Key features:
- Add files with `addFile(path, content)`
- Add directories with `addDirectory(path)`
- Open files with `open(path, mode)`
- List directory contents with `listDir(path)`
- Clear all files with `clear()`

### MockTime.h
Mock time functions for deterministic testing:
- `MockTimeState` class: Controls mock time
- `setMillis(ms)`: Set current time in milliseconds
- `advanceMillis(ms)`: Advance time by milliseconds
- `setTime(t)`: Set current time in seconds since epoch
- `advanceTime(seconds)`: Advance time by seconds
- `reset()`: Reset all time values to zero

### FS.h
Wrapper that includes MockFS.h when UNIT_TEST is defined.

## Usage in Tests

### Basic Setup

```cpp
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
```

### Using MockFS

```cpp
void test_file_operations() {
    fs::FS mockSD;
    
    // Add a test file
    mockSD.addFile("/test.txt", "Hello, World!");
    
    // Open and read the file
    fs::File file = mockSD.open("/test.txt", "r");
    TEST_ASSERT_TRUE(file);
    
    uint8_t buffer[20];
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(13, bytesRead);
    
    file.close();
}
```

### Using MockTime

```cpp
void test_timing() {
    // Set initial time
    MockTimeState::setMillis(1000);
    TEST_ASSERT_EQUAL(1000, millis());
    
    // Advance time
    MockTimeState::advanceMillis(500);
    TEST_ASSERT_EQUAL(1500, millis());
    
    // Test delay
    delay(100);
    TEST_ASSERT_EQUAL(1600, millis());
}
```

### Using Mock Serial

```cpp
void test_logging() {
    Serial.begin(115200);
    Serial.println("Test message");  // Prints to stdout
    Serial.print("Value: ");
    Serial.println(42);
}
```

### Using ArduinoJson Mock

```cpp
void test_json_parsing() {
    fs::FS mockSD;
    
    // Create a JSON config file
    std::string jsonContent = R"({
        "WIFI_SSID": "TestNetwork",
        "UPLOAD_HOUR": 14,
        "GMT_OFFSET_SECONDS": -28800
    })";
    mockSD.addFile("/config.json", jsonContent);
    
    // Parse the JSON
    fs::File file = mockSD.open("/config.json", "r");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    TEST_ASSERT_FALSE(error);
    
    // Access values with defaults
    String ssid = doc["WIFI_SSID"] | "";
    int hour = doc["UPLOAD_HOUR"] | 12;
    long offset = doc["GMT_OFFSET_SECONDS"] | 0L;
    
    TEST_ASSERT_EQUAL_STRING("TestNetwork", ssid.c_str());
    TEST_ASSERT_EQUAL(14, hour);
    TEST_ASSERT_EQUAL(-28800, offset);
}
```

## Conditional Compilation

All mocks are only active when `UNIT_TEST` is defined. This is automatically set by the native test environment in platformio.ini.

In production code:
- Real Arduino.h is used
- Real FS.h from ESP32 SDK is used
- Real time functions from ESP32 SDK are used

In test code:
- Mock implementations are used
- Tests run on native platform (not ESP32)
- Deterministic behavior for reliable testing

## Adding New Mocks

To add a new mock:

1. Create a new header file in this directory (e.g., `MockWiFi.h`)
2. Wrap all mock code with `#ifdef UNIT_TEST`
3. Implement the interface that matches the real component
4. Document the mock in this README
5. Include the mock in test files as needed

Example:

```cpp
#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#ifdef UNIT_TEST

class MockWiFi {
private:
    bool connected;
    
public:
    MockWiFi() : connected(false) {}
    
    void begin(const char* ssid, const char* password) {
        connected = true;
    }
    
    bool isConnected() {
        return connected;
    }
    
    void disconnect() {
        connected = false;
    }
};

extern MockWiFi WiFi;

#endif // UNIT_TEST

#endif // MOCK_WIFI_H
```
