# Development Guide

This document is for developers who want to build, modify, or contribute to the ESP32 CPAP Data Uploader project.

## Table of Contents
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Development Setup](#development-setup)
- [Building](#building)
- [Testing](#testing)
- [Release Process](#release-process)
- [Contributing](#contributing)

---

## Architecture

### Core Components

- **Config** - Manages configuration from SD card (`config.json`) and secure credential storage
- **SDCardManager** - Handles SD card sharing with CPAP machine
- **WiFiManager** - Manages WiFi station mode connection
- **FileUploader** - Orchestrates file upload to remote endpoints

### Upload Management

- **UploadStateManager** - Tracks which files/folders have been uploaded using checksums
- **TimeBudgetManager** - Enforces time limits on SD card access (respects CPAP priority)
- **ScheduleManager** - Manages daily upload scheduling with NTP time synchronization

### Upload Backends

- **SMBUploader** - Uploads files to SMB/CIFS shares (Windows, NAS, Samba)
- **WebDAVUploader** - Uploads to WebDAV servers (TODO: placeholder)
- **SleepHQUploader** - Direct upload to SleepHQ service (TODO: placeholder)

### Supporting Components

- **Logger** - Circular buffer logging system with web API access
- **TestWebServer** - Optional web server for development/testing

### Design Principles

1. **Dependency Injection** - Components receive dependencies via constructor
2. **Single Responsibility** - Each class has one clear purpose
3. **Testability** - Core logic is unit tested
4. **Feature Flags** - Backends are conditionally compiled
5. **Resource Management** - Careful memory and flash usage
6. **Security by Default** - Credentials stored securely in flash memory unless explicitly disabled

---

## Project Structure

```
├── src/                      # Main application code
│   ├── main.cpp             # Application entry point
│   ├── Config.cpp           # Configuration management
│   ├── SDCardManager.cpp    # SD card control
│   ├── WiFiManager.cpp      # WiFi connection handling
│   ├── FileUploader.cpp     # File upload orchestration
│   ├── UploadStateManager.cpp # Upload state tracking
│   ├── TimeBudgetManager.cpp  # Time budget enforcement
│   ├── ScheduleManager.cpp    # Upload scheduling
│   ├── SMBUploader.cpp        # SMB upload implementation
│   ├── TestWebServer.cpp      # Test web server (optional)
│   ├── Logger.cpp             # Circular buffer logging
│   ├── WebDAVUploader.cpp     # WebDAV upload (placeholder)
│   └── SleepHQUploader.cpp    # SleepHQ upload (placeholder)
├── include/                  # Header files
│   ├── pins_config.h        # Pin definitions for SD WIFI PRO
│   └── *.h                  # Component headers
├── test/                     # Unit tests (72 tests, all passing)
│   ├── test_time_budget_manager/
│   ├── test_config/
│   ├── test_webserver/
│   ├── test_native/
│   ├── test_schedule_manager/
│   └── mocks/               # Mock implementations
├── components/               # ESP-IDF components
│   └── libsmb2/             # SMB2/3 client library (cloned)
├── scripts/                  # Build and release scripts
│   ├── setup_libsmb2.sh     # Setup SMB library
│   └── prepare_release.sh   # Create release packages
├── release/                  # Release package files
│   ├── upload.sh            # macOS/Linux upload script
│   ├── upload.bat           # Windows upload script
│   └── README.md            # End-user documentation
├── docs/                     # Developer documentation
├── setup.sh                 # Quick project setup
├── build_upload.sh          # Quick build and upload
├── monitor.sh               # Quick serial monitor
└── platformio.ini           # PlatformIO configuration
```

---

## Development Setup

### Prerequisites

- Python 3.7 or later
- Git
- USB drivers for ESP32 (CH340 or CP210x)

### Quick Setup

Run the automated setup script (caution this has only been tested on linux):

```bash
./setup.sh
```

This will:
1. Create Python virtual environment
2. Install PlatformIO
3. Clone and configure libsmb2 component
4. Install all dependencies

### Manual Setup

If you prefer manual setup:

```bash
# 1. Create Python venv
python3 -m venv venv

# 2. Install PlatformIO
source venv/bin/activate
pip install platformio

# 3. Setup libsmb2 (if using SMB upload)
./scripts/setup_libsmb2.sh

# 4. Install dependencies
pio pkg install
```

### IDE Setup

The project works with:
- **PlatformIO IDE** (VS Code extension) - Recommended
- **Command line** - All features available via CLI

---

## Building

### Quick Build & Upload

```bash
./build_upload.sh
```

### Manual Build

```bash
source venv/bin/activate
pio run -e pico32              # Build only
pio run -e pico32 -t upload    # Build and upload
```

### Build Targets

```bash
pio run -e pico32              # Build firmware
pio run -e pico32 -t upload    # Upload to device
pio run -e pico32 -t clean     # Clean build
pio run -e pico32 -t size      # Show memory usage
pio run -e pico32 -t erase     # Erase flash
```

### Build Configuration

Edit `platformio.ini` to configure:

**Feature Flags:**
```ini
build_flags = 
    -DENABLE_SMB_UPLOAD          ; Enable SMB/CIFS upload
    ; -DENABLE_WEBDAV_UPLOAD     ; Enable WebDAV (TODO)
    ; -DENABLE_SLEEPHQ_UPLOAD    ; Enable SleepHQ (TODO)
    -DENABLE_TEST_WEBSERVER      ; Enable test web server
```

**Logging:**
```ini
build_flags =
    -DLOG_BUFFER_SIZE=32768      ; 32KB log buffer (default: 2KB)
    -DCORE_DEBUG_LEVEL=3         ; ESP32 core debug level (0-5)
    -DENABLE_VERBOSE_LOGGING     ; Enable debug logs (compiled out by default)
```

**Debug Logging:** By default, `LOG_DEBUG()` and `LOG_DEBUGF()` macros are compiled out (zero overhead). Enable with `-DENABLE_VERBOSE_LOGGING` to see detailed diagnostics including progress updates, state details, and troubleshooting information. Saves ~10-15KB flash and ~35-75ms per upload session when disabled.

### Memory Usage

Current build (with SMB enabled):
- **Flash:** 25.9% (1,058,372 / 4,096,000 bytes) - ESP32 has 4MB flash, app partition is 3MB
- **RAM:** 10.5% (47,165 / 450,000 bytes estimated) - Static: 34KB, Dynamic buffers grow with usage

**Dynamic Memory Analysis:** The `.upload_state.json` file grows with DATALOG folder count (~30 bytes per folder, ~100 bytes per file checksum), and the system now dynamically allocates DynamicJsonDocument buffers sized at 2x the file size for loading and 1.5x estimated size for saving, allowing it to handle thousands of folders limited only by available RAM (~340KB free, supporting ~10+ years of daily CPAP usage before memory exhaustion).

---

## Testing

### Unit Tests

Run all tests:
```bash
source venv/bin/activate
pio test -e native
```

Run specific test suite:
```bash
pio test -e native -f test_config
pio test -e native -f test_time_budget_manager
pio test -e native -f test_schedule_manager
```

### Test Coverage

- `test_time_budget_manager`: 18 tests - Time budget and rate calculation
- `test_config`: 14 tests - Configuration parsing and validation
- `test_webserver`: 9 tests - Web server endpoints
- `test_native`: 9 tests - Mock infrastructure
- `test_schedule_manager`: 22 tests - Scheduling and NTP

### Hardware Testing

See [Hardware Testing Checklist](#hardware-testing-checklist) below.

---

## Release Process

### Creating a Release Package

```bash
./scripts/prepare_release.sh
```

This creates a timestamped zip file in `release/` containing:
- `firmware.bin` - Precompiled firmware
- `upload.sh` - macOS/Linux upload script
- `upload.bat` - Windows upload script
- `README.md` - End-user documentation

### Windows Release

For Windows releases, download `esptool.exe` from [espressif/esptool releases](https://github.com/espressif/esptool/releases) and place it in `release/` before running `prepare_release.sh`.

### Version Tagging

Use semantic versioning (MAJOR.MINOR.PATCH):

```bash
git tag -a v0.3.0 -m "Release v0.3.0: Description"
git push origin v0.3.0
```

---

## Hardware Testing Checklist

### Prerequisites

- [ ] SD WIFI PRO dev board connected with SD WIFI PRO inserted
- [ ] `config.json` created on SD card
- [ ] WiFi network available
- [ ] SMB share accessible and writable
- [ ] internet access (required for NTP server)

### Test Procedure

1. **Flash Firmware**
   ```bash
   ./build_upload.sh
   ```

2. **Monitor Serial Output**
   ```bash
   ./monitor.sh
   ```

3. **Verify Startup**
   - [ ] Config loaded successfully
   - [ ] WiFi connected
   - [ ] NTP time synchronized
   - [ ] SMB connection established

4. **Test Upload**
   - [ ] Files uploaded to SMB share
   - [ ] `.upload_state.json` created on SD card
   - [ ] No errors in serial output

5. **Test Web Interface** (if enabled)
   - [ ] Access `http://<device-ip>/`, you can get the device IP from the serial port after a reset.
   - [ ] Trigger manual upload
   - [ ] View logs
   - [ ] Check status

### Common Issues

See [BUILD_TROUBLESHOOTING.md](BUILD_TROUBLESHOOTING.md) for detailed troubleshooting.

---

## Contributing

### Code Style

- Follow existing code style
- Use meaningful variable names
- Add comments for complex logic (comments are code too)

### Adding Features

1. Write tests first (TDD approach)
2. Implement feature
3. Update documentation
4. Test on hardware

### Adding Upload Backends

To add a new backend (e.g., FTP):

1. Create `include/FTPUploader.h` and `src/FTPUploader.cpp`
2. Wrap with feature flag: `#ifdef ENABLE_FTP_UPLOAD`
3. Add flag to `platformio.ini`
4. Update `FileUploader` to instantiate new backend
5. Add tests
6. Document in `FEATURE_FLAGS.md`

### Pull Request Process

1. Fork the repository
2. Create feature branch
3. Make changes with tests
4. Ensure all tests pass
5. Update documentation
6. Submit pull request

---

## Useful Commands

```bash
# Development
./setup.sh                     # Initial setup
./build_upload.sh              # Build and upload
./monitor.sh                   # Serial monitor

# PlatformIO
pio run                        # Build
pio run -t upload              # Upload
pio run -t clean               # Clean
pio test -e native             # Run tests
pio device list                # List serial ports
pio device monitor             # Serial monitor

# Release
./scripts/prepare_release.sh   # Create release package

# Cleanup
rm -rf venv/ components/libsmb2/ .pio/  # Clean for portability
```

---

## Credential Security

### Overview

The system supports two credential storage modes:

1. **Secure Mode (Default)** - Credentials stored in ESP32 NVS (Non-Volatile Storage)
2. **Plain Text Mode** - Credentials stored in `config.json` on SD card

### Preferences Library Usage

The system uses the ESP32 Preferences library (a high-level wrapper around NVS) for secure credential storage.

**Namespace:** `cpap_creds`

**Stored Credentials:**
- `wifi_pass` - WiFi password
- `endpoint_pass` - Endpoint (SMB/WebDAV) password

**Key Methods:**

```cpp
// Initialize Preferences
bool Config::initPreferences() {
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        LOG("ERROR: Failed to initialize Preferences");
        return false;
    }
    return true;
}

// Store credential
bool Config::storeCredential(const char* key, const String& value) {
    size_t written = preferences.putString(key, value);
    return written > 0;
}

// Load credential
String Config::loadCredential(const char* key, const String& defaultValue) {
    return preferences.getString(key, defaultValue);
}

// Close Preferences
void Config::closePreferences() {
    preferences.end();
}
```

### Migration Process

When secure mode is enabled (default), the system automatically migrates credentials on first boot:

1. **Detection:** System checks if credentials in `config.json` are censored
2. **Migration:** If plain text detected, credentials are:
   - Stored in NVS using Preferences library
   - Verified by reading back from NVS
   - Censored in `config.json` (replaced with `***STORED_IN_FLASH***`)
3. **Subsequent Boots:** Credentials loaded directly from NVS

**Migration Flow:**

```
Boot → Load config.json
  ├─ STORE_CREDENTIALS_PLAIN_TEXT = true?
  │    └─ YES → Use plain text (no migration)
  │
  └─ NO/ABSENT → Check if censored
       ├─ YES → Load from NVS
       └─ NO → Migrate to NVS + Censor config.json
```

### Error Handling

**Preferences Initialization Failure:**
- System logs error
- Falls back to plain text mode
- Continues operation with credentials from `config.json`

**NVS Write Failure:**
- System logs detailed error
- Keeps credentials in plain text
- Does not censor `config.json`

**NVS Read Failure:**
- System logs warning
- Attempts to use `config.json` values
- May trigger re-migration if plain text available

### Web Interface Protection

The TestWebServer component respects credential storage mode:

```cpp
void TestWebServer::handleConfig() {
    // Check if credentials are secured
    if (config->areCredentialsInFlash()) {
        // Return censored values
        doc["wifi_password"] = Config::CENSORED_VALUE;
        doc["endpoint_password"] = Config::CENSORED_VALUE;
        doc["credentials_secured"] = true;
    } else {
        // Return actual values (plain text mode)
        doc["wifi_password"] = config->wifiPassword;
        doc["endpoint_password"] = config->endpointPassword;
        doc["credentials_secured"] = false;
    }
}
```

### Security Considerations

**Protected Against:**
- Physical SD card access (credentials not in `config.json`)
- Web interface credential exposure (censored in responses)
- Serial log exposure (credentials never logged)

**Not Protected Against:**
- Flash memory dumps (requires physical device access + tools)
- Malicious firmware (can read NVS)
- JTAG/SWD debug access (hardware debugging interface)

**Best Practices:**
- Use secure mode for production deployments
- Physically secure the device
- Change credentials if device is lost/stolen
- Use plain text mode only for development

### Performance Impact

**Memory Usage:**
- Preferences object: ~20 bytes
- Storage mode flags: 2 bytes
- Total additional RAM: ~22 bytes

**Boot Time:**
- Migration (first boot only): +100-200ms
- Subsequent boots: No measurable impact

**Flash Wear:**
- NVS writes: Only during migration or credential changes
- Expected writes: 1-10 over device lifetime
- ESP32 flash endurance: 100,000 write cycles
- Conclusion: Not a concern

### Testing Credential Security

**Unit Tests:**
```bash
# Run Config tests (includes Preferences operations)
pio test -e native -f test_config
```

**Hardware Tests:**

1. **Test Secure Mode (Default):**
   ```bash
   # 1. Create config.json with plain text credentials
   # 2. Set STORE_CREDENTIALS_PLAIN_TEXT to false or omit
   # 3. Flash and boot device
   # 4. Check serial output for migration messages
   # 5. Verify config.json shows ***STORED_IN_FLASH***
   # 6. Verify WiFi connects and uploads work
   # 7. Check web interface shows censored values
   ```

2. **Test Plain Text Mode:**
   ```bash
   # 1. Set STORE_CREDENTIALS_PLAIN_TEXT to true
   # 2. Flash and boot device
   # 3. Verify credentials remain in config.json
   # 4. Verify web interface shows actual values
   ```

3. **Test Migration:**
   ```bash
   # 1. Start with plain text config
   # 2. Change STORE_CREDENTIALS_PLAIN_TEXT to false
   # 3. Reboot device
   # 4. Verify migration occurs
   # 5. Verify system continues to work
   ```

## Architecture Decisions

### Why Preferences Library?

- High-level wrapper around ESP32 NVS
- Simple key-value API
- Automatic namespace management
- Built into ESP32 Arduino Core
- No external dependencies

### Why libsmb2?

- Full SMB2/3 protocol support
- Mature, well-tested library
- Acceptable binary footprint (~220-270KB)
- Active maintenance

### Why Time Budgeting?

CPAP machines need regular SD card access. Time budgeting ensures:
- Short upload sessions, default 5 seconds, configurable.
- CPAP machine gets priority
- Uploads resume automatically

### Why Circular Buffer Logging?

- Fixed memory usage (configurable)
- No SD card writes (reduces wear)
- Thread-safe for dual-core ESP32

### Why Feature Flags?

- Minimize binary size
- Enable only needed backends
- Faster compilation
- Cleaner code separation

---

## Performance Considerations

### Flash Usage

- Base firmware: ~800KB
- SMB backend: +220-270KB
- WebDAV backend: +50-80KB (estimated)
- Total available: 3MB (huge_app partition)

### RAM Usage

- Static allocation: ~47KB
- Log buffer: 32KB (configurable)
- SMB buffers: ~32KB during upload
- Total available: 320KB

### Upload Performance

- Default rate: 40 KB/s (conservative estimate)
- Actual rate: Varies by network and share, during tests the transfer achieved 130KB/s
- Rate tracking: Running average of last 5 uploads
- Adaptive budgeting: Increases on repeated failures

---

## References

- [BUILD_TROUBLESHOOTING.md](BUILD_TROUBLESHOOTING.md) - Build issues
- [FEATURE_FLAGS.md](FEATURE_FLAGS.md) - Backend selection
- [LIBSMB2_INTEGRATION.md](LIBSMB2_INTEGRATION.md) - SMB integration details
- [PlatformIO Docs](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [libsmb2 GitHub](https://github.com/sahlberg/libsmb2)

