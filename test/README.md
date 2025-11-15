# Test Directory

This directory contains unit tests for the SD WIFI PRO auto uploader project.

## Structure

- `test_native/` - Native unit tests that run on the host machine (not on ESP32)
- `mocks/` - Mock implementations of hardware-dependent components for testing

## Running Tests

To run the native tests:

```bash
pio test -e native
```

## Test Framework

Tests use the Unity test framework, which is included with PlatformIO.

## Writing Tests

Each test file should:
1. Include the Unity framework: `#include <unity.h>`
2. Include the component being tested
3. Define test functions with `void test_*()` naming
4. Use Unity assertions (TEST_ASSERT_*, TEST_ASSERT_EQUAL_*, etc.)
5. Register tests in `setUp()` and `tearDown()` functions if needed

Example:
```cpp
#include <unity.h>
#include "MyComponent.h"

void test_component_initialization() {
    MyComponent component;
    TEST_ASSERT_TRUE(component.begin());
}

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_component_initialization);
    return UNITY_END();
}
```
