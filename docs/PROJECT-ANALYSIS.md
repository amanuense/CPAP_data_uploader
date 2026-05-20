# Project Analysis: CPAP Data Uploader

## Scope

ESP32 firmware that reads CPAP therapy data from an SD card and uploads it to network shares (SMB/CIFS) or SleepHQ cloud. Supports ResMed Series 9/10/11 machines via the FYSETC SD-WIFI-PRO adapter. The firmware uses a finite state machine with hardware bus-activity detection (PCNT) to safely share SD card access between the CPAP machine and the ESP32.

## Language & Platform

- **C++11** targeting **ESP32** (ESP32-PICO-D4) via **PlatformIO** with the **Arduino framework**
- **Native unit tests** using the Unity test framework (run on host, not ESP32)
- Python scripts for build/version/release tooling

## Project Structure

```
├── include/          # Header files (.h) — one-per-class, PascalCase
├── src/              # Implementation files (.cpp) — mirrors include/
├── test/             # Native unit tests + mocks
│   ├── mocks/        # Arduino/ESP32 mock headers for host testing
│   ├── test_*/       # Per-component test directories
│   └── README.md, TESTING_GUIDE.md
├── docs/             # Architecture docs, specs, screenshots, vendor PDFs
│   ├── 01-FINDINGS.md .. 04-IMPLEMENTATION-PLAN.md  # numbered design docs
│   ├── specs/        # Feature specifications
│   └── vendor/       # Datasheets
├── scripts/          # Build helpers, library setup, release prep
├── release/          # Release notes, user README, upload scripts
├── components/       # External libraries (libsmb2, gitignored)
├── platformio.ini    # PlatformIO build config (pico32 + native)
├── CMakeLists.txt    # Native test build (CMake)
└── *.sh / *.bat      # Setup, monitor, build scripts
```

## Code Style & Conventions

- **Naming**: PascalCase for classes/files (`CpapWebServer`, `UploadStateManager`), camelCase for methods/variables (`handleUploading()`, `isInUploadWindow()`)
- **Namespaces**: Only anonymous namespaces for file-local helpers in `.cpp` files
- **Headers**: Standard `#ifndef` guards, `#include <Arduino.h>` as first include, project headers with `#include "Header.h"`
- **Logging**: Custom `LOGF()`, `LOG_ERROR()`, `LOG_WARN()`, `LOG_DEBUGF()` macros wrapping Arduino `Serial`
- **Todo/Section markers**: `// ============================================================================`, `// ──`, `// ──`
- **Comments**: Doxygen-style `/** */` for important functions, inline `//` for rationale
- **Bracing**: Allman style (braces on new lines) for functions, K&R-ish for control flow
- **Memory**: Heavy use of fixed-size arrays (no dynamic allocation at runtime), pre-allocated buffers, careful heap tracking via `ESP.getFreeHeap()` / `ESP.getMaxAllocHeap()`
- **Conditional compilation**: `#ifdef ENABLE_SMB_UPLOAD`, `#ifdef UNIT_TEST`, etc.
- **Feature flags**: Defined in `platformio.ini` `build_flags`
- **State management**: Journal + snapshot files on SD card (`.upload_state.v2.*`), bounded arrays, periodic compaction

## Architecture

- **FSM-driven loop** in `main.cpp` with 8 states: IDLE, LISTENING, ACQUIRING, UPLOADING, RELEASING, COOLDOWN, COMPLETE, MONITORING
- **Hardware PCNT** peripheral on GPIO 33 for non-blocking SD bus activity detection
- **Dual-core**: Upload runs as FreeRTOS task on Core 0; web server + loop on Core 1
- **Two modes**: Smart (continuous loop, 24/7) and Scheduled (window-based)
- **Two upload backends**: SMB/CIFS (via libsmb2) and SleepHQ Cloud (HTTPS REST)
- **Progressive Web App** web interface served directly from ESP32 flash
- **Upload state tracking**: Per-file checksums (MD5) for settings files, folder-level tracking for DATALOG, size-first detection for recent data

## Key Technical Constraints

- SD card bus is shared via hardware mux (GPIO 26): only one of ESP32 or CPAP can access the card at any time
- CS_SENSE (GPIO 33) monitors host-side bus activity while mux is in CPAP mode
- ESP32 is powered by the CPAP's SD slot — limited power budget, brownout risk on some AirSense 11 models
- 4MB flash, ~220KB RAM available after framework overhead
- No dynamic memory allocation during upload paths (pre-allocated buffers throughout)
