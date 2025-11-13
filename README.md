# SD WIFI PRO auto uploader

This project intends to create a simple solution to upload new files daily to a remote endpoint using SD WIFI PRO hardware.
The intended usage is backups of CPAP machine data stored in the SD card, the data in the card is read only.
TODO: add information on how this data can be used with Oscar or SleepHQ

## Requirements
- Standalone device which connects to the network specified in the CONFIG file stored in the root of the SD card and uploads new files based on the schedule listed in the SD card.
- Files in the SD card are read only to prevent data loss.
- Files in remote endpoint are write only.


## Hardware
- ESP32-PICO-V3-02 ([SD WIFI PRO](https://www.fysetc.com/products/fysetc-upgrade-sd-wifi-pro-with-card-reader-module-run-wireless-by-esp32-chip-web-server-reader-uploader-3d-printer-parts))
- 8GB built-in Flash
- SD 7.0 compatible interface

## Architecture

This project uses a clean, class-based architecture with explicit dependency injection:

- **Config** - Manages configuration from SD card
- **SDCardManager** - Handles SD card sharing with CPAP machine
- **WiFiManager** - Manages WiFi station mode connection
- **FileUploader** - Handles file upload to remote endpoints

## Project Structure
```
├── src/                  # Main application code
│   ├── main.cpp         # Application entry point
│   ├── Config.cpp       # Configuration management
│   ├── SDCardManager.cpp # SD card control
│   ├── WiFiManager.cpp  # WiFi connection handling
│   └── FileUploader.cpp # File upload logic
├── include/             # Header files
│   ├── pins_config.h    # Pin definitions for SD WIFI PRO
│   ├── Config.h
│   ├── SDCardManager.h
│   ├── WiFiManager.h
│   └── FileUploader.h
├── venv/                # Python virtual environment
├── docs/                # Documentation and reference firmware
├── platformio.ini       # PlatformIO configuration
└── README.md           # This file
```

## Libraries (managed by PlatformIO)
- **ArduinoJson** - JSON library for config file parsing
- **SD_MMC** - Built-in ESP32 SDIO library (4-bit mode)
- **WiFi** - Built-in ESP32 WiFi library
- **FS** - Built-in ESP32 filesystem library

## Setup
1. Activate Python virtual environment: `source venv/bin/activate`
2. Install dependencies: `pio pkg install`
3. Build: `pio run`
4. Upload: `pio run -t upload`

## Quick Build
```bash
source venv/bin/activate
pio pkg install
pio run -t upload
```

## Monitor
`pio device monitor`

## Pin Configuration
See `include/pins_config.h` for pin definitions specific to SD WIFI PRO hardware.



# Runtime usage

## CONFIG FILE

Create a `config.json` file in the root of your SD card with the following format:

```json
{
  "WIFI_SSID": "YourNetworkName",
  "WIFI_PASS": "YourNetworkPassword",
  "SCHEDULE": "daily",
  "ENDPOINT": "http://your-server.com/upload",
  "ENDPOINT_TYPE": "WEBDAV",
  "ENDPOINT_USER": "username",
  "ENDPOINT_PASS": "password"
}
```

### Configuration Fields

- **WIFI_SSID**: SSID of the WiFi network to connect to (required)
- **WIFI_PASS**: Password for the WiFi network (required)
- **SCHEDULE**: Upload schedule (e.g., "daily", "hourly") - TODO: implement scheduling logic
- **ENDPOINT**: Remote location where files will be uploaded (required)
- **ENDPOINT_TYPE**: Type of endpoint - `WEBDAV`, `SMB`, or `SLEEPHQ`
  - `WEBDAV`: WebDAV share (e.g., NextCloud) - Format: `http://address/folder`
  - `SMB`: Windows share - Format: `//address/folder` (e.g., `//10.0.0.5/backups/cpap_data`) - TODO: implement
  - `SLEEPHQ`: Direct upload to SleepHQ - TODO: implement
- **ENDPOINT_USER**: Username for the remote endpoint
- **ENDPOINT_PASS**: Password for the remote endpoint

## How It Works

1. **Startup**: Device reads config from SD card
2. **WiFi Connection**: Connects to specified WiFi network in station mode
3. **File Detection**: Periodically checks SD card for new files
4. **SD Card Sharing**: Respects CPAP machine access - only reads when CPAP is not using the card
5. **Upload**: Uploads new files to configured endpoint
6. **Monitoring**: Continuously monitors for new files and maintains WiFi connection

## Development Status

### Implemented
- ✅ SD card sharing with CPAP machine
- ✅ Configuration file loading from SD card
- ✅ WiFi station mode connection
- ✅ Class-based architecture with dependency injection

### TODO
- ⏳ File upload implementations (WebDAV, SMB, SleepHQ)
- ⏳ File tracking (which files have been uploaded)
- ⏳ Schedule-based upload logic
- ⏳ Error handling and retry logic
- ⏳ Status LED indicators
- ⏳ Low power mode when idle

## Reference Firmware

The `docs/` folder contains reference implementations:
- **SdWiFiBrowser** - Basic WiFi file browser
- **ESP3D v3.0** - Advanced firmware with WebDAV/FTP support

These are for reference only and not used directly in this project.
