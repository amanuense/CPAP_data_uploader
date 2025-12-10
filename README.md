# ESP32 CPAP Data Uploader

Automatically upload CPAP therapy data from your SD card to network storage. Tested with ResMed CPAP machines, may work with other brands that use SD cards.

**Features:**
- Automatic daily uploads to Windows shares, NAS, or Samba servers
- Secure credential storage in ESP32 flash memory (optional)
- Respects CPAP machine access to SD card
- Tracks uploaded files (no duplicates)
- Scheduled uploads with timezone support
- Web interface for monitoring and testing
- Automatic retry mechanism with progress tracking

## For End Users

**Want to use this?** Download the latest release package with precompiled firmware and easy upload tools.

### Known issues
- Sometimes the CPAP machine will complain about SD card not working. This is likely caused by CPAP machine attempting to access the SD card at the same time as our firmware. When this happens just reinsert the SD card.
- The upload doesn't create the DATALOG folder. We are tracking this issue. To prevent this issue, copy the data of the SD card into the destination folder or manually create the DATALOG folder.

👉 [Download Latest Release](../../releases) (includes upload tools for Windows, Mac, and Linux)

See the [User Guide](release/README.md) for complete setup and usage instructions.


## Hardware

**Required:** [SD WIFI PRO](https://www.fysetc.com/products/fysetc-upgrade-sd-wifi-pro-with-card-reader-module-run-wireless-by-esp32-chip-web-server-reader-uploader-3d-printer-parts) adapter
- ESP32-PICO-D4 microcontroller
- 4MB Flash memory
- SD card interface (compatible with CPAP SD cards)
- WiFi 2.4GHz

## For Developers

**Want to build or contribute?** See the [Development Guide](docs/DEVELOPMENT.md) for:
- Architecture overview
- Build instructions
- Testing procedures
- Contributing guidelines

## Quick Start

### 1. Get the Hardware
Purchase the [SD WIFI PRO](https://www.fysetc.com/products/fysetc-upgrade-sd-wifi-pro-with-card-reader-module-run-wireless-by-esp32-chip-web-server-reader-uploader-3d-printer-parts).

### 2. Download Firmware
Download the latest release package from the [Releases page](../../releases).

### 3. Upload Firmware
Follow the instructions in the release package to upload firmware to your device.

### 4. Configure
Create a `config.json` file on your SD card with your WiFi and network share settings.

### 5. Done!
Insert the SD card and power on. The device will automatically upload new CPAP data daily.

## Configuration Example

Create `config.json` on your SD card:

```json
{
  "WIFI_SSID": "YourNetworkName",
  "WIFI_PASS": "YourPassword",
  "ENDPOINT": "//192.168.1.100/cpap_backups",
  "ENDPOINT_TYPE": "SMB",
  "ENDPOINT_USER": "username",
  "ENDPOINT_PASS": "password",
  "UPLOAD_HOUR": 12,
  "GMT_OFFSET_HOURS": -8
}
```

See the [User Guide](release/README.md) for complete configuration reference.

### Credential Security

By default, the system stores WiFi and endpoint passwords securely in ESP32 flash memory (NVS) and censors them in `config.json`. This protects your credentials if someone accesses your SD card or web interface.

**Secure Mode (Default - Recommended):**
- Set `"STORE_CREDENTIALS_PLAIN_TEXT": false` or omit the field
- Credentials automatically migrated to flash memory on first boot
- `config.json` updated with `***STORED_IN_FLASH***` placeholders
- Web interface shows censored values

**Plain Text Mode (Development/Debugging):**
- Set `"STORE_CREDENTIALS_PLAIN_TEXT": true`
- Credentials remain visible in `config.json`
- Web interface shows actual values

**Migration Process:**
1. First boot with secure mode: System reads plain text credentials from `config.json`
2. Stores them securely in ESP32 flash memory (NVS)
3. Updates `config.json` with censored placeholders
4. Subsequent boots: Loads credentials from flash memory

**Security Considerations:**
- ✅ Protected against SD card physical access
- ✅ Protected against web interface credential exposure
- ⚠️ Not protected against flash memory dumps (requires physical device access)
- ⚠️ Migration is one-way (cannot retrieve credentials back to plain text)

For production use, always use secure mode (default). Use plain text mode only for development or debugging.



## How It Works

1. **Device reads configuration** from `config.json` on SD card
2. **Connects to WiFi** and synchronizes time with internet
3. **Waits for scheduled time** (once per day at configured hour)
4. **Uploads new files** to your network share
   - Takes brief control of SD card (default 5 seconds)
   - Uploads DATALOG folders, root files, and settings
   - Tracks what's been uploaded (no duplicates)
   - Releases SD card for CPAP machine use
5. **Repeats daily** automatically

The device respects your CPAP machine's need for SD card access by keeping upload sessions short and releasing control immediately after each session.

## Capacity & Longevity

**Memory:** Currently the firmware with SMB upload and web server enabled can track upload state for **10+ years** of daily CPAP usage (3,000+ folders) before exhausting available RAM.

**Storage:** Based on typical CPAP data size (~2.7 MB per day), the 8GB SD WIFI PRO card can store approximately **8+ years** (3,000+ days) of CPAP data before filling up.

## Project Status

**Current Version:** v0.3.2 (development)

**Status:** ✅ Production Ready
- Hardware tested and validated
- Integration tested with real CPAP data
- All unit tests passing (131 tests)
- SMB/CIFS upload fully implemented
- Web interface remains responsive during uploads
- Automatic retry mechanism with progress tracking

**Supported Upload Methods:**
- ✅ SMB/CIFS (Windows shares, NAS, Samba)
- ⏳ WebDAV (planned)
- ⏳ SleepHQ direct upload (planned)

## Future Improvements

- Implement FreeRTOS tasks for true concurrent web server operation during uploads
- Add WebDAV upload support
- Add SleepHQ direct upload support

## Support & Documentation

- **User Guide:** [release/README.md](release/README.md) - Setup and usage instructions
- **Developer Guide:** [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) - Build and contribute
- **Troubleshooting:** See user guide or developer guide
- **Issues:** Report bugs or request features via GitHub issues

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

**What this means:**
- ✅ You can use this software for free
- ✅ You can modify the source code
- ✅ You can distribute modified versions
- ⚠️ **Any distributed versions (modified or not) must remain free and open source**
- ⚠️ Modified versions must also be licensed under GPL-3.0

This project uses libsmb2 (LGPL-2.1), which is compatible with GPL-3.0.

See [LICENSE](LICENSE) file for full terms.

---

**Made for CPAP users who want automatic, reliable data backups.**

