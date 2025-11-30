# SD Card Scan Failure Tests

## Overview

This test suite validates the critical bug fix for SD card scanning when the CPAP machine is using the SD card. The bug caused folders to be incorrectly marked as "completed" when scan operations failed due to SD card access conflicts.

## Bug Description

**Original Issue:**
When the CPAP machine was actively using the SD card:
1. Folder scan operations would fail silently
2. Empty scan results were treated as "empty folder"
3. Folders were incorrectly marked as completed
4. New folders would only be detected after removing SD card from CPAP

**Impact:**
- Upload state corruption
- Missing data uploads
- Inconsistent folder tracking

## Test Coverage

### 1. `test_normal_folder_scan_success`
Verifies that folders with files are properly scanned and marked complete under normal conditions.

### 2. `test_empty_folder_marked_complete`
Confirms that truly empty folders (accessible but no files) are correctly marked as complete.

### 3. `test_folder_access_failure_not_marked_complete` ⚠️ CRITICAL
**Key Test:** Ensures that when folder access fails (CPAP using SD card), the folder is NOT marked as complete and retry count is incremented.

### 4. `test_scan_empty_but_folder_inaccessible` ⚠️ CRITICAL
**Key Test:** Validates that when scan returns empty results but folder verification fails, the system treats it as an error (not completion).

### 5. `test_folder_accessible_after_cpap_release`
Tests the retry mechanism: folder fails on first attempt (CPAP using), succeeds on second attempt (CPAP released).

### 6. `test_multiple_retry_attempts`
Verifies that retry counter increments correctly across multiple failed attempts and folder is never marked complete during failures.

### 7. `test_distinguish_scan_failure_from_empty`
Confirms the system can distinguish between:
- Truly empty folder (accessible, no files) → mark complete
- Scan failure (inaccessible) → increment retry, don't mark complete

## Running Tests

```bash
# Activate virtual environment
source venv/bin/activate

# Run this specific test
sudo -E $(which pio) test -e native -f test_sd_scan_failure

# Run all tests
sudo -E $(which pio) test -e native
```

## Implementation Details

The test uses a `FailingMockFS` that can simulate:
- CPAP using SD card (all access fails)
- Specific folders being inaccessible
- Folders with various file contents

This allows comprehensive testing of error handling paths without requiring actual hardware.

## Related Files

- `src/FileUploader.cpp` - Contains the fixed scanning logic
- `include/FileUploader.h` - FileUploader interface
- `test/mocks/MockFS.h` - Base mock filesystem

## Success Criteria

All 7 tests must pass to ensure:
1. Normal operations work correctly
2. Scan failures are properly detected
3. Folders are never incorrectly marked as complete
4. Retry mechanism functions properly
5. System can recover when SD card becomes accessible
