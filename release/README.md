# ESP32 CPAP Data Uploader - User Guide

This package contains precompiled firmware for automatically uploading CPAP data from your SD card to a network share.

## What This Does

- Automatically uploads CPAP data files from SD card to your network storage (Windows share, NAS, etc.)
- Uploads once per day at a scheduled time
- Respects CPAP machine access to the SD card (short upload sessions)
- Tracks which files have been uploaded (no duplicates)
- **Tested with ResMed CPAP machines** (may work with other brands)

**Capacity:** The firmware can track upload state for **10+ years** of daily CPAP usage (3,000+ folders). The 8GB SD WIFI PRO card can store approximately **8+ years** (3,000+ days) of CPAP data based on typical usage (~2.7 MB per day).

---

## Quick Start

### 1. Upload Firmware

**Important:** Connect the SD WIFI PRO to the development board with switches set to:
- Switch 1: OFF
- Switch 2: ON

**Windows:**
1. Find your COM port (see "Finding Your Serial Port" below)
2. Run the upload script with your COM port:
```cmd
upload.bat COM3
```

**macOS/Linux:**
```bash
./upload.sh /dev/ttyUSB0
```

Replace `COM3` or `/dev/ttyUSB0` with your actual serial port.

### 2. Create Configuration File

Create a file named `config.json` in the root of your SD card with your settings:

```json
{
  "WIFI_SSID": "YourNetworkName",
  "WIFI_PASS": "YourNetworkPassword",
  "ENDPOINT": "//192.168.1.100/cpap_backups",
  "ENDPOINT_TYPE": "SMB",
  "ENDPOINT_USER": "username",
  "ENDPOINT_PASS": "password",
  "UPLOAD_HOUR": 12,
  "SESSION_DURATION_SECONDS": 5,
  "MAX_RETRY_ATTEMPTS": 3,
  "GMT_OFFSET_HOURS": 0
}
```

### 3. Insert SD Card and Power On

Insert the SD card into your CPAP machine's SD slot and power it on. The device will:
1. Connect to WiFi
2. Sync time with internet
3. Wait for the scheduled upload time
4. Upload new files to your network share

---

## Configuration Reference

### Network Settings

**WIFI_SSID** (required)
- Your WiFi network name
- Example: `"HomeNetwork"`
- Note: ESP32 only supports 2.4GHz WiFi (not 5GHz)

**WIFI_PASS** (required)
- Your WiFi password
- Example: `"MySecurePassword123"`

### Upload Destination

**ENDPOINT** (required)
- Network location where files will be uploaded
- Format: `//server/share` or `//server/share/folder`
- Examples:
  - Windows PC: `"//192.168.1.100/cpap_backups"`
  - NAS device: `"//nas.local/backups"`
  - With subfolder: `"//192.168.1.5/backups/cpap_data"`

**ENDPOINT_TYPE** (required)
- Type of network share
- Value: `"SMB"` (currently only SMB/CIFS is supported)

**ENDPOINT_USER** (required)
- Username for the network share
- Example: `"john"` or `"DOMAIN\\john"`
- Use empty string `""` for guest access (if share allows)

**ENDPOINT_PASS** (required)
- Password for the network share
- Example: `"password123"`
- Use empty string `""` for guest access

### Schedule Settings

**UPLOAD_HOUR** (optional, default: 12)
- Hour of day (0-23) when uploads should occur in GMT/UTC time
- Uses 24-hour format
- **Important:** This is in GMT/UTC, not your local time. Use GMT_OFFSET_HOURS to adjust for your timezone.
- Examples:
  - `12` = noon GMT
  - `0` = midnight GMT
  - `14` = 2 PM GMT
  - `23` = 11 PM GMT

**SESSION_DURATION_SECONDS** (optional, default: 5)
- Maximum time (in seconds) to hold SD card access per session
- Keeps sessions short so CPAP machine can access card
- Recommended: 5-10 seconds
- Increase if you have very large files

**MAX_RETRY_ATTEMPTS** (optional, default: 3)
- Number of retry attempts before increasing time budget
- After this many interrupted uploads, time budget multiplies
- Recommended: 3

**GMT_OFFSET_HOURS** (optional, default: 0)
- Your timezone offset from GMT/UTC in hours
- Used to convert UPLOAD_HOUR from GMT to your local time
- Examples:
  - `0` = UTC/GMT
  - `-8` = Pacific Time (PST) - if UPLOAD_HOUR is 12, uploads at 4 AM PST
  - `-5` = Eastern Time (EST) - if UPLOAD_HOUR is 12, uploads at 7 AM EST
  - `+1` = Central European Time (CET) - if UPLOAD_HOUR is 12, uploads at 1 PM CET
  - `+10` = Australian Eastern Time (AEST) - if UPLOAD_HOUR is 12, uploads at 10 PM AEST
- For daylight saving time, adjust the offset (e.g., `-7` for PDT instead of `-8` for PST)

---

## Common Configuration Examples

### US Pacific Time (PST/PDT)
```json
{
  "WIFI_SSID": "HomeNetwork",
  "WIFI_PASS": "password",
  "ENDPOINT": "//192.168.1.100/cpap",
  "ENDPOINT_TYPE": "SMB",
  "ENDPOINT_USER": "john",
  "ENDPOINT_PASS": "password",
  "UPLOAD_HOUR": 12,
  "GMT_OFFSET_HOURS": -8
}
```

### Europe (CET)
```json
{
  "WIFI_SSID": "HomeNetwork",
  "WIFI_PASS": "password",
  "ENDPOINT": "//nas.local/backups",
  "ENDPOINT_TYPE": "SMB",
  "ENDPOINT_USER": "user",
  "ENDPOINT_PASS": "password",
  "UPLOAD_HOUR": 14,
  "GMT_OFFSET_HOURS": 1
}
```

### NAS with Guest Access
```json
{
  "WIFI_SSID": "HomeNetwork",
  "WIFI_PASS": "password",
  "ENDPOINT": "//192.168.1.50/public",
  "ENDPOINT_TYPE": "SMB",
  "ENDPOINT_USER": "",
  "ENDPOINT_PASS": "",
  "UPLOAD_HOUR": 12,
  "GMT_OFFSET_HOURS": 0
}
```

---

## How It Works

### First Boot
1. Device reads `config.json` from SD card
2. Connects to WiFi network
3. Synchronizes time with internet (NTP)
4. Loads upload history from `.upload_state.json` (if exists)

### Daily Upload Cycle
1. Waits until configured `UPLOAD_HOUR`
2. Takes control of SD card (CPAP must wait briefly)
3. Uploads new/changed files in priority order:
   - DATALOG folders (newest first)
   - Root files (identification.json, STR.edf, journal.jnl)
   - SETTINGS files
4. Releases SD card after session or time budget exhausted
5. Saves progress to `.upload_state.json`

### Smart File Tracking
- **DATALOG folders**: Tracks completion (all files uploaded = done)
- **Root/SETTINGS files**: Tracks checksums (only uploads if changed)
- Never uploads the same file twice

### SD Card Sharing
- Only takes control when needed for uploads
- Keeps sessions short (default 5 seconds)
- Releases control immediately after session
- CPAP machine can access card anytime device doesn't have control

---

## Finding Your Serial Port

**Note:** Connect the SD WIFI PRO to the development board with switches set to:
- Switch 1: OFF
- Switch 2: ON

### Windows
1. Open Device Manager (Win+X, then select Device Manager)
2. Expand "Ports (COM & LPT)"
3. Look for "USB-SERIAL CH340" or "Silicon Labs CP210x"
4. Note the COM port number (e.g., COM3, COM4)

**Tip:** If you run `upload.bat` without arguments, it will show detailed instructions for finding your COM port.

### macOS
```bash
ls /dev/cu.*
```
Look for `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`

### Linux
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```
Usually `/dev/ttyUSB0` or `/dev/ttyACM0`

---

## Test Web Server (Optional)

The firmware includes an optional test web server for development and troubleshooting.

### Accessing the Web Interface

1. After device connects to WiFi, note the IP address from serial monitor
2. Open browser and go to: `http://<device-ip>/`

### Available Features

**Status Page** (`http://<device-ip>/`)
- System uptime and current time
- Next scheduled upload time
- Time budget remaining
- Pending files count
- Current configuration

**Trigger Upload** (`http://<device-ip>/trigger-upload`)
- Force immediate upload (bypasses schedule)
- Useful for testing without waiting

**View Status** (`http://<device-ip>/status`)
- JSON format system status
- Useful for monitoring

**Reset State** (`http://<device-ip>/reset-state`)
- Clears upload history
- Forces re-upload of all files
- Useful for testing from clean state

**View Configuration** (`http://<device-ip>/config`)
- Shows current config.json values
- Useful for verifying configuration

**View Logs** (`http://<device-ip>/logs`)
- Shows recent log messages from circular buffer
- Useful for troubleshooting

### Security Warning
⚠️ The web server has no authentication. Only use on trusted networks.

---

## Troubleshooting

### Firmware Upload Issues

**"Failed to connect to ESP32"**
- Verify SD WIFI PRO is connected to development board
- Check switches: Switch 1 OFF, Switch 2 ON
- Check USB cable (must be data cable, not charge-only)
- Try different USB port
- Verify correct serial port selected

**"Permission denied" (Linux/Mac)**
- Run with sudo: `sudo ./upload.sh /dev/ttyUSB0`
- Or add user to dialout group: `sudo usermod -a -G dialout $USER` (logout/login required)

**"Port not found"**
- Make sure ESP32 is connected
- Install USB drivers (CH340 or CP210x)
- Check Device Manager (Windows) or `ls /dev/tty*` (Linux/Mac)

### WiFi Connection Issues

**Device doesn't connect to WiFi**
- Verify WIFI_SSID and WIFI_PASS are correct
- Ensure WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check WiFi network is in range
- Try moving device closer to router

**WiFi connects but no internet**
- Check router internet connection
- Verify router allows device to access internet
- Check firewall settings

### SD Card Issues

**SD card not detected**
- The SD WIFI PRO is an integrated SD card with ESP32 chip
- Verify the device is properly inserted into the CPAP machine
- Check that the device is receiving power

**config.json not found**
- Ensure file is named exactly `config.json` (lowercase)
- Place file in root of SD card (not in a folder)
- Verify file is valid JSON (use online JSON validator)

### SMB Connection Issues

**Cannot connect to SMB share**
- Verify ENDPOINT format: `//server/share`
- Test share is accessible from another computer on the same network
- Check ENDPOINT_USER and ENDPOINT_PASS are correct
- Try IP address instead of hostname (e.g., `//192.168.1.100/share` instead of `//nas.local/share`)

**Authentication fails**
- Verify username and password are correct
- For Windows, try: `DOMAIN\\username` format
- Check share permissions allow write access
- Try guest access (empty user/pass) if share allows

### Upload Issues

**Files not uploading**
- Check it's past the UPLOAD_HOUR time
- Verify internet connection for time sync
- Check SMB connection is working
- View logs via web interface: `http://<device-ip>/logs`

**Upload incomplete**
- Increase SESSION_DURATION_SECONDS if files are large
- Check available space on network share
- Verify network stability
- Check logs for specific errors

**Same files uploading repeatedly**
- Check `.upload_state.json` exists on SD card
- Verify SD card has write permission
- Try reset state via web interface

### Time Sync Issues

**Time not synchronized**
- Verify internet connection
- Check firewall allows NTP (UDP port 123)
- Try different NTP server (requires firmware modification)

**Wrong timezone**
- Verify GMT_OFFSET_HOURS is correct for your location
- Remember to adjust for daylight saving time

### Getting More Information

**View Serial Monitor Output**
```bash
# Windows (using PlatformIO)
pio device monitor

# Linux/Mac
screen /dev/ttyUSB0 115200
# or
sudo pio device monitor
```

**View Logs via Web Interface**
```
http://<device-ip>/logs
```

**Check Upload State**
- Look for `.upload_state.json` on SD card
- Contains upload history and retry counts

---

## File Structure

### On SD Card
```
/
├── config.json              # Your configuration (you create this)
├── .upload_state.json       # Upload tracking (auto-created)
├── identification.json      # CPAP identification
├── identification.crc       # Checksum
├── STR.edf                  # Summary data
├── journal.jnl              # Journal file
├── DATALOG/                 # Therapy data folders
│   ├── 20241114/           # Date-named folders (YYYYMMDD)
│   │   ├── file1.edf
│   │   └── file2.edf
│   └── 20241113/
└── SETTINGS/                # Settings folder
    ├── CurrentSettings.json
    └── CurrentSettings.crc
```

### On Network Share
Files are uploaded maintaining the same structure:
```
//server/share/
├── identification.json
├── identification.crc
├── STR.edf
├── journal.jnl
├── DATALOG/
│   ├── 20241114/
│   │   ├── file1.edf
│   │   └── file2.edf
│   └── 20241113/
└── SETTINGS/
    ├── CurrentSettings.json
    └── CurrentSettings.crc
```

---

## Manual Firmware Upload

If the upload scripts don't work, you can use esptool directly:

### Windows
```cmd
esptool.exe --chip esp32 --port COM3 --baud 460800 write_flash 0x10000 firmware.bin
```

### macOS/Linux
```bash
# Install esptool
pip install esptool

# Upload firmware
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash 0x10000 firmware.bin
```

---

## Support

For issues, questions, or contributions, visit the project repository.

**Hardware:** ESP32-PICO-D4 (SD WIFI PRO)  
**Firmware Version:** v0.3.0

