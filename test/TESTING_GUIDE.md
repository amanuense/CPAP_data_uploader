# Testing Guide

This guide explains how to write and run unit tests for the SD WIFI PRO auto uploader project.

## Quick Start

Run all native tests:
```bash
pio test -e native
```

Run tests with verbose output:
```bash
pio test -e native -vv
```

## Test Structure

Tests are organized in the `test/` directory:

```
test/
├── README.md              # Overview of test directory
├── TESTING_GUIDE.md       # This file
├── mocks/                 # Mock implementations
│   ├── README.md          # Mock documentation
│   ├── Arduino.h          # Mock Arduino core
│   ├── Arduino.cpp        # Mock Arduino implementation
│   ├── FS.h               # Mock filesystem wrapper
│   ├── MockFS.h           # Mock filesystem implementation
│   └── MockTime.h         # Mock time functions
└── test_native/           # Native unit tests
    └── test_*.cpp         # Test files
```

## Writing Tests

### Basic Test Template

```cpp
#include <unity.h>
#include "Arduino.h"
#include "FS.h"
#include "MockTime.h"

// Include the component you're testing
#include "YourComponent.h"

void setUp(void) {
    // Called before each test
    MockTimeState::reset();
}

void tearDown(void) {
    // Called after each test
}

void test_your_feature() {
    // Arrange
    YourComponent component;
    
    // Act
    bool result = component.doSomething();
    
    // Assert
    TEST_ASSERT_TRUE(result);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_your_feature);
    return UNITY_END();
}
```

### Testing with MockFS

```cpp
void test_file_operations() {
    fs::FS mockSD;
    
    // Add test files
    mockSD.addFile("/test.txt", "Hello, World!");
    mockSD.addDirectory("/DATALOG");
    mockSD.addFile("/DATALOG/20241101/data.edf", "test data");
    
    // Test file operations
    fs::File file = mockSD.open("/test.txt", "r");
    TEST_ASSERT_TRUE(file);
    TEST_ASSERT_EQUAL(13, file.size());
    
    uint8_t buffer[20];
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(13, bytesRead);
    
    file.close();
}
```

### Testing with MockTime

```cpp
void test_timing() {
    // Set initial time
    MockTimeState::setMillis(1000);
    TEST_ASSERT_EQUAL(1000, millis());
    
    // Advance time
    MockTimeState::advanceMillis(500);
    TEST_ASSERT_EQUAL(1500, millis());
    
    // Test with seconds
    MockTimeState::setTime(1699876800);
    TEST_ASSERT_EQUAL(1699876800, time(nullptr));
}
```

### Testing Components

When testing components that depend on ESP32-specific libraries:

1. **Don't include the source file in the build** - The native environment excludes all source files by default
2. **Include the component header in your test** - This allows you to test the interface
3. **Create a test-specific implementation if needed** - For components that need special handling in tests

Example for testing TimeBudgetManager:

```cpp
#include <unity.h>
#include "Arduino.h"
#include "MockTime.h"

// Include the actual implementation
#include "../src/TimeBudgetManager.cpp"

void setUp(void) {
    MockTimeState::reset();
}

void tearDown(void) {
}

void test_budget_initialization() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(1000);
    manager.startSession(5);  // 5 second session
    
    TEST_ASSERT_TRUE(manager.hasBudget());
    TEST_ASSERT_EQUAL(5000, manager.getRemainingBudgetMs());
}

void test_budget_consumption() {
    TimeBudgetManager manager;
    
    MockTimeState::setMillis(1000);
    manager.startSession(5);
    
    // Advance time by 2 seconds
    MockTimeState::advanceMillis(2000);
    
    TEST_ASSERT_EQUAL(3000, manager.getRemainingBudgetMs());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_budget_initialization);
    RUN_TEST(test_budget_consumption);
    return UNITY_END();
}
```

## Unity Assertions

Common Unity assertions:

```cpp
// Boolean assertions
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);

// Equality assertions
TEST_ASSERT_EQUAL(expected, actual);
TEST_ASSERT_EQUAL_INT(expected, actual);
TEST_ASSERT_EQUAL_STRING(expected, actual);

// Comparison assertions
TEST_ASSERT_GREATER_THAN(threshold, actual);
TEST_ASSERT_LESS_THAN(threshold, actual);

// Null assertions
TEST_ASSERT_NULL(pointer);
TEST_ASSERT_NOT_NULL(pointer);

// Array assertions
TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, count);
TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, count);

// Float assertions
TEST_ASSERT_EQUAL_FLOAT(expected, actual);
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual);
```

## Best Practices

1. **Keep tests focused** - Each test should verify one specific behavior
2. **Use descriptive test names** - Name tests like `test_component_behavior_condition`
3. **Reset state between tests** - Use `setUp()` to reset mocks and state
4. **Test edge cases** - Include tests for boundary conditions and error cases
5. **Don't test implementation details** - Test the public interface, not internal state
6. **Make tests deterministic** - Use mocks to control time, filesystem, and other external factors

## Debugging Tests

If a test fails:

1. Run with verbose output: `pio test -e native -vv`
2. Add debug output: `Serial.println("Debug message");`
3. Check mock state: Verify that mocks are set up correctly
4. Isolate the test: Comment out other tests to focus on the failing one

## Adding New Tests

To add a new test file:

1. Create a new file in `test/test_native/` with the prefix `test_`
2. Include necessary headers (unity.h, Arduino.h, etc.)
3. Write test functions with the `test_` prefix
4. Implement `setUp()` and `tearDown()` if needed
5. Create a `main()` function that runs all tests
6. Run `pio test -e native` to execute

## Continuous Integration

Tests can be integrated into CI/CD pipelines:

```bash
# Run tests and exit with error code if any fail
pio test -e native

# Run tests with verbose output for CI logs
pio test -e native -vv
```

## Troubleshooting

### "No such file or directory" errors

If you see errors about missing headers:
- Check that the mock is included in `test/mocks/`
- Verify that `-I test/mocks` is in the build flags
- Ensure the component doesn't depend on ESP32-specific libraries

### Linker errors

If you see undefined reference errors:
- Check that the source file is included in the test
- Verify that all dependencies are mocked or included
- Consider including the `.cpp` file directly in the test

### Test hangs or crashes

If tests hang or crash:
- Check for infinite loops in the code
- Verify that mocks are properly initialized
- Look for null pointer dereferences
- Use `Serial.println()` to add debug output

## Further Reading

- [Unity Test Framework Documentation](https://github.com/ThrowTheSwitch/Unity)
- [PlatformIO Unit Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- [Test-Driven Development Best Practices](https://martinfowler.com/bliki/TestDrivenDevelopment.html)
