# ESP32 CPAP Data Uploader - User Guide

This package contains precompiled firmware for automatically uploading CPAP data from your SD card to a network share or the Cloud (**SleepHQ**).

## Table of Contents
- [🚀 Get Started in 3 Steps](#-get-started-in-3-steps-seriously-its-this-easy)
- [What This Does](#what-this-does)
- [Firmware Options](#firmware-options)
- [Quick Start](#quick-start)
  - [0. Initialize SD Card](#0-initialize-the-sd-card)
  - [1. Upload Firmware](#1-upload-firmware)
  - [2. Create Configuration](#2-create-configuration-file)
  - [3. Insert SD Card](#3-insert-sd-card-and-power-on)
- [Configuration Reference](#configuration-reference)
- [Common Configuration Examples](#common-configuration-examples)
- [How It Works](#how-it-works)
- [Finding Your Serial Port](#finding-your-serial-port)
- [Web Interface](#web-interface-optional)
- [Troubleshooting](#troubleshooting)
- [File Structure](#file-structure)
- [Package Contents](#package-contents)
- [Legal & Support](#legal--trademarks)

---

## 🚀 Get Started in 3 Steps (Seriously, It's This Easy!)

### Step 1: Flash the Firmware
Upload the firmware using the included scripts (details below in [Quick Start](#quick-start))

### Step 2: Create `config.txt` 
Create a file named `config.txt` on your SD card with **just these lines**:

**👇👇👇 Click the option you want to use** (or click ▸ to expand additional options):

---

<details>
<summary><b>📤 For Network Share (SMB)</b></summary>

```ini
# WiFi
WIFI_SSID = YourWiFiName
WIFI_PASSWORD = YourWiFiPassword

# Upload Destination
ENDPOINT_TYPE = SMB
ENDPOINT = //192.168.1.100/cpap_backups
ENDPOINT_USER = username
ENDPOINT_PASSWORD = password
```

**That's it!** Replace the values with your actual WiFi and network share details.
</details>

<details>
<summary><b>☁️ For SleepHQ Cloud</b></summary>

```ini
# WiFi
WIFI_SSID = YourWiFiName
WIFI_PASSWORD = YourWiFiPassword

# SleepHQ
ENDPOINT_TYPE = CLOUD
CLOUD_CLIENT_ID = your-sleephq-client-id
CLOUD_CLIENT_SECRET = your-sleephq-client-secret
```

**That's it!** Replace with your actual WiFi and SleepHQ credentials.
</details>

<details>
<summary><b>🔄 For Both SMB + SleepHQ (Dual Upload)</b></summary>

```ini
# WiFi
WIFI_SSID = YourWiFiName
WIFI_PASSWORD = YourWiFiPassword

# Dual Upload
ENDPOINT_TYPE = SMB,CLOUD
ENDPOINT = //192.168.1.100/cpap_backups
ENDPOINT_USER = username
ENDPOINT_PASSWORD = password
CLOUD_CLIENT_ID = your-sleephq-client-id
CLOUD_CLIENT_SECRET = your-sleephq-client-secret
```

**That's it!** Provide both SMB and SleepHQ credentials to upload to both destinations.
</details>

---

**All other settings are optional and have smart defaults.**

### Step 3: Insert SD Card & Done!
Insert the SD card into your CPAP machine. The device will automatically:
- ✅ Connect to WiFi
- ✅ Sync time
- ✅ Wait for therapy to end (Smart Mode)
- ✅ Upload your CPAP data

**💻 Web Interface:** Once running, access the dashboard at **`http://cpap.local`** to monitor uploads, view logs, and manage settings.

**No complex setup. No JSON syntax. Just simple key = value pairs.**

Want more control? See [Configuration Reference](#configuration-reference) for all optional settings.

---

## What This Does

- Automatically uploads CPAP data files from SD card to your network storage or the Cloud (SleepHQ)
- Uploads automatically when therapy ends (Smart Mode) or at a scheduled time
- Respects CPAP machine access to the SD card (short upload sessions)
- **Local Network Discovery (mDNS):** Access the device via `http://cpap.local` instead of IP address
- Tracks which files have been uploaded (no duplicates)
- Automatically creates directories on remote shares as needed
- **Supported CPAP Machines:** ResMed Series 9, 10, and 11 (other brands not supported)

**Capacity:** The firmware tracks upload state for the last **1 year** (rolling window). Older data is automatically ignored based on the `MAX_DAYS` configuration (default 365). The 8GB SD WIFI PRO card can store approximately **8+ years** (3,000+ days) of CPAP data based on typical usage (~2.7 MB per day).

---

## Firmware Options

This package includes two firmware versions:

### **OTA Firmware** (Recommended)
- **File:** `firmware-ota.bin` (complete firmware with bootloader)
- **Upgrade file:** `firmware-ota-upgrade.bin` (for web OTA updates)
- **Features:** Web-based firmware updates via `/ota` interface
- **Partition:** 1.5MB app space (dual OTA partitions)
- **Best for:** Production use, remote deployments
- **Update method:** 
  - Initial flash: Use `firmware-ota.bin` with upload scripts
  - Upgrades: Upload `firmware-ota-upgrade.bin` via web browser

### **Standard Firmware**
- **File:** `firmware-standard.bin` (complete firmware with bootloader)
- **Features:** Maximum app space, no OTA capability
- **Partition:** 3MB app space
- **Best for:** Development, maximum feature space, users who prefer manual updates
- **Update method:** USB/Serial upload using `firmware-standard.bin` (can be re-flashed anytime)

---

## Quick Start

### 0. Initialize the SD card

Insert the SD card in your CPAP machine and allow for the CPAP machine to format it.

### 1. Upload Firmware

#### First time or when upgrading from a non OTA version
**Important:** Connect the SD WIFI PRO to the development board with switches set to:
- Switch 1: OFF
- Switch 2: ON

**Windows:**
1. Ensure Python 3.7+ is installed (download from https://python.org)
2. Find your COM port (see "Finding Your Serial Port" below)
3. Run the appropriate upload script:
```cmd
REM For OTA firmware (recommended)
upload-ota.bat COM3

REM For standard firmware
upload-standard.bat COM3
```

The script will automatically:
- Create a virtual environment
- Install esptool
- Flash the complete firmware (bootloader + partitions + app)

**macOS/Linux:**
```bash
# For OTA firmware (default)
./upload.sh /dev/ttyUSB0

# For standard firmware
./upload.sh /dev/ttyUSB0 standard
```

Replace `COM3` or `/dev/ttyUSB0` with your actual serial port.

#### When upgrading OTA firmware via web interface

1. Go to the CPAP Data uploader web interface: `http://<device-ip>/ota`
2. Use **Method 1** to upload `firmware-ota-upgrade.bin` from your computer
3. Or use **Method 2** to download firmware directly from GitHub
4. Device will restart automatically after update

**Note:** For standard firmware, you must re-flash via USB/Serial using the upload scripts.

### 2. Create Configuration File

Create a file named `config.txt` in the root of your SD card.

**Option A: Cloud Upload (SleepHQ)**
```ini
WIFI_SSID = YourNetworkName
WIFI_PASSWORD = YourNetworkPassword
HOSTNAME = cpap

ENDPOINT_TYPE = CLOUD
CLOUD_CLIENT_ID = your-sleephq-client-id
CLOUD_CLIENT_SECRET = your-sleephq-client-secret

GMT_OFFSET_HOURS = 0
```

**Option B: Network Share (SMB)**
```ini
WIFI_SSID = YourNetworkName
WIFI_PASSWORD = YourNetworkPassword
HOSTNAME = cpap

ENDPOINT_TYPE = SMB
ENDPOINT = //192.168.1.100/cpap_backups
ENDPOINT_USER = username
ENDPOINT_PASSWORD = password

GMT_OFFSET_HOURS = 0
```

**Syntax Notes:** 
- Lines starting with `#` or `//` are comments.
- Spaces around `=` are optional.
- Keys are case-insensitive.

**Common mistake that causes "SSID is empty" errors:**
Missing `config.txt` file, or using invalid Key-Value syntax.

### 3. Insert SD Card and Power On

Insert the SD card into your CPAP machine's SD slot and power it on. The device will:
1. Connect to WiFi
2. Sync time with internet
3. Wait for upload eligibility based on mode
4. Upload new files to your network share

---

## Configuration Reference

### Network Settings

**WIFI_SSID** (required)
- Your WiFi network name
- Example: `WIFI_SSID = HomeNetwork`
- Note: ESP32 only supports 2.4GHz WiFi (not 5GHz)

**WIFI_PASSWORD** (required)
- Your WiFi password
- Example: `WIFI_PASSWORD = MySecurePassword123`

**HOSTNAME** (optional, default: "cpap")
- Device hostname for local network discovery
- Access via: `http://hostname.local` (e.g., `http://cpap.local`)
- Example: `HOSTNAME = airsense11`


### Upload Destination

**ENDPOINT** (required)
- Network location where files will be uploaded
- Format: `//server/share` or `//server/share/folder`
- Examples:
  - Windows PC: `ENDPOINT = //192.168.1.100/cpap_backups`
  - NAS device: `ENDPOINT = //nas.local/backups`
  - With subfolder: `ENDPOINT = //192.168.1.5/backups/cpap_data`

**ENDPOINT_TYPE** (required)
- Type of upload destination
- Values: 
  - `SMB` - Upload to network share
  - `CLOUD` - Upload to SleepHQ
  - `SMB,CLOUD` - Upload to both (simultaneously)

**CLOUD_CLIENT_ID** (required for CLOUD)
- Your SleepHQ Client ID
- Example: `CLOUD_CLIENT_ID = your-client-id`

**CLOUD_CLIENT_SECRET** (required for CLOUD)
- Your SleepHQ Client Secret
- Example: `CLOUD_CLIENT_SECRET = your-client-secret`

**ENDPOINT_USER** (required for SMB)
- Username for the network share
- Example: `ENDPOINT_USER = john` or `ENDPOINT_USER = DOMAIN\john`
- Leave empty/omit for guest access (if share allows)

**ENDPOINT_PASSWORD** (required for SMB)
- Password for the network share
- Example: `ENDPOINT_PASSWORD = password123`
- Leave empty/omit for guest access

### Schedule Settings

**UPLOAD_MODE** (optional, default: "smart")
- `scheduled`: uploads in the configured time window
- `smart` (recommended): starts shortly after therapy ends (activity + inactivity detection)

**UPLOAD_START_HOUR** (optional, default: 9)
- Start of upload window (0-23, local time)

**UPLOAD_END_HOUR** (optional, default: 21)
- End of upload window (0-23, local time)
- If start == end, uploads are allowed 24/7

**INACTIVITY_SECONDS** (optional, default: 125)
- Required SD bus idle time before smart mode starts uploading
- Range: 10-3600

**EXCLUSIVE_ACCESS_MINUTES** (optional, default: 5)
- Maximum time per upload session while holding SD access
- Range: 1-30

**COOLDOWN_MINUTES** (optional, default: 10)
- Pause between upload sessions
- Range: 1-60

**MAX_DAYS** (optional, default: 365)
- Maximum number of days in the past to check for upload eligibility
- Range: 1-366
- Helps prevent infinite loops on very old data and manages memory usage
- **Note:** Requires valid time synchronization (NTP) to function correctly

**GMT_OFFSET_HOURS** (optional, default: 0)
- Your timezone offset from GMT/UTC in hours
- Used for local time calculations (upload window + status display)
- Examples:
  - `0` = UTC/GMT
  - `-8` = Pacific Time (PST)
  - `-5` = Eastern Time (EST)
  - `+1` = Central European Time (CET)
  - `+10` = Australian Eastern Time (AEST)
- For daylight saving time, adjust the offset (e.g., `-7` for PDT instead of `-8` for PST)

### Power Management Settings

**CPU_SPEED_MHZ** (optional, default: 240)
- CPU frequency in MHz (80, 160, 240)
- Lower values save power but slow down uploads
- Recommended for low power: `160`

**WIFI_TX_PWR** (optional, default: "high")
- WiFi transmission power ("low", "mid", "high")
- Lower values reduce range but save power

**WIFI_PWR_SAVING** (optional, default: "none")
- WiFi power saving mode ("none", "mid", "max")
- "max" saves significant power but may increase latency

### Debugging Settings

**DEBUG** (optional, default: false)
- Enable verbose diagnostic logging at runtime (no re-flash required)
- When `true`, adds per-folder pre-flight scan lines to the log:
  `[FileUploader] Pre-flight scan: folder=20260219 completed=1 pending=0 recent=1`
- Also appends `[res fh= ma= fd=]` heap and resource stats to every log line
- Useful for diagnosing upload scheduling issues; disable when not needed
- Example: `DEBUG = true`

**LOG_TO_SD_CARD** (optional, default: false)
- Enable logging system messages to `/debug.log` file on SD card
- **WARNING**: Can prevent the CPAP machine from reliably accessing the SD card
- Use only temporarily for troubleshooting, and only with `UPLOAD_MODE`=`"scheduled"` and an upload window outside normal therapy times
- Automatically disabled if file operations fail
- Example: `true` or `false`

### Credential Security

The system automatically secures your WiFi and endpoint passwords by moving them to the ESP32's internal flash memory.

**How it works:**
1. You put plain text passwords in `config.txt`
2. On first boot, the device reads them and saves them to secure storage
3. The device updates `config.txt` replacing passwords with `***STORED_IN_FLASH***`
4. On subsequent boots, it uses the secure values

**To update a password:**
- Just replace `***STORED_IN_FLASH***` with your new plain text password in `config.txt`
- The device will detect the change, update the secure storage, and re-censor the file

**To disable security (for debugging):**
- Add `STORE_CREDENTIALS_PLAIN_TEXT = true` to your `config.txt`
- Passwords will remain visible in the file

### Webhook Notifications

These optional settings fire HTTP POST requests on successful completion of a sync session. They are useful for notifying external systems (e.g., SleepLab, Healthchecks.io, Home Assistant) that new CPAP data is ready.

**SLEEPLAB_DOMAIN** (optional)
> The base URL of your SleepLab instance. When combined with `SLEEPLAB_USER_ID`, the firmware POSTs to `{SLEEPLAB_DOMAIN}/api/import/webhook/{SLEEPLAB_USER_ID}` after a successful sync.
>
> Trailing slashes are automatically stripped.
>
> Example: `https://sleeplab.example.com`

**SLEEPLAB_USER_ID** (optional)
> Your user ID for the SleepLab instance. Combined with `SLEEPLAB_DOMAIN` to form the webhook URL.
>
> Example: `user_abc123`

**GENERIC_WEBHOOK_URL** (optional)
> A full URL to POST to after every successful sync. Useful for pinging uptime monitors (Healthchecks.io) or triggering automations (Home Assistant).
>
> Example: `https://hc-ping.com/your-uuid-here`

**Behavior:**
- Webhooks fire **only** when all folders complete successfully (session returns COMPLETE)
- Each webhook has a strict **5000ms timeout** — a timeout or failure does not affect the upload result
- If both SleepLab and Generic are configured, SleepLab fires first, then Generic (sequential)
- HTTP clients are block-scoped to free TLS/socket resources immediately after each POST
- A `vTaskDelay(10)` yield separates the two webhooks to feed the FreeRTOS watchdog

---

## Common Configuration Examples

### SleepHQ (Cloud Only)
```ini
WIFI_SSID = HomeNetwork
WIFI_PASSWORD = password
ENDPOINT_TYPE = CLOUD
CLOUD_CLIENT_ID = your-client-id
CLOUD_CLIENT_SECRET = your-client-secret
UPLOAD_MODE = smart
GMT_OFFSET_HOURS = 0
```

### Dual Upload (SMB + SleepHQ)
```ini
WIFI_SSID = HomeNetwork
WIFI_PASSWORD = password

ENDPOINT_TYPE = SMB,CLOUD
ENDPOINT = //nas.local/backups
ENDPOINT_USER = user
ENDPOINT_PASSWORD = pass

CLOUD_CLIENT_ID = your-client-id
CLOUD_CLIENT_SECRET = your-client-secret

UPLOAD_MODE = smart
GMT_OFFSET_HOURS = 0
```

### US Pacific Time (PST/PDT)
```ini
WIFI_SSID = HomeNetwork
WIFI_PASSWORD = password
ENDPOINT = //192.168.1.100/cpap
ENDPOINT_TYPE = SMB
ENDPOINT_USER = john
ENDPOINT_PASSWORD = password
UPLOAD_MODE = scheduled
UPLOAD_START_HOUR = 8
UPLOAD_END_HOUR = 22
INACTIVITY_SECONDS = 125
EXCLUSIVE_ACCESS_MINUTES = 5
COOLDOWN_MINUTES = 10
GMT_OFFSET_HOURS = -8
```

### Europe (CET)
```ini
WIFI_SSID = HomeNetwork
WIFI_PASSWORD = password
ENDPOINT = //nas.local/backups
ENDPOINT_TYPE = SMB
ENDPOINT_USER = user
ENDPOINT_PASSWORD = password
UPLOAD_MODE = scheduled
UPLOAD_START_HOUR = 7
UPLOAD_END_HOUR = 21
INACTIVITY_SECONDS = 125
EXCLUSIVE_ACCESS_MINUTES = 5
COOLDOWN_MINUTES = 10
GMT_OFFSET_HOURS = 1
```

### NAS with Guest Access
```ini
WIFI_SSID = HomeNetwork
WIFI_PASSWORD = password
ENDPOINT = //192.168.1.50/public
ENDPOINT_TYPE = SMB
ENDPOINT_USER = 
ENDPOINT_PASSWORD = 
UPLOAD_MODE = smart
UPLOAD_START_HOUR = 8
UPLOAD_END_HOUR = 22
INACTIVITY_SECONDS = 125
EXCLUSIVE_ACCESS_MINUTES = 5
COOLDOWN_MINUTES = 10
GMT_OFFSET_HOURS = 0
```

---

## How It Works

### First Boot
1. Device reads `config.txt` from SD card
2. Connects to WiFi network
3. Synchronizes time with internet (NTP)
4. Loads upload history from `.upload_state.v2` + `.upload_state.v2.log` (if present)

### Daily Upload Cycle
1. Waits for upload eligibility based on configured mode (`UPLOAD_MODE`)
   - Smart mode: shortly after therapy ends (activity + inactivity detection)
   - Scheduled mode: during configured upload window
2. **Pre-flight scan** checks for new/changed files (SD-only, no network)
3. Takes control of SD card (only when CPAP is idle)
4. **Staged upload processing** (optimizes memory usage):
   - SMB pass: Upload to network share
   - Cloud pass: Upload to SleepHQ (only if files exist)
5. Uploads new/changed files in priority order:
   - **SMB**: Root/SETTINGS files first, then DATALOG folders (newest first)
   - **Cloud**: DATALOG folders first (newest first), then Root/SETTINGS files (only if DATALOG files uploaded)
6. **SMB:** Automatically creates directories on remote share
   **Cloud:** Associates data with your SleepHQ account (OAuth only if needed)
7. Releases SD card after session or time budget exhausted
8. **Automatic heap recovery** reboots if memory fragmented (seamless fast-boot)
9. Saves progress to separate state files for each backend (`.upload_state.v2.smb`/`.cloud` + journals)

### Smart File Tracking
- **DATALOG folders**: Tracks completion (all files uploaded = done)
- **Root/SETTINGS files**: Tracks checksums (only uploads if changed)
- Never uploads the same file twice

### SD Card Sharing
- **Passive Operation:** Only accesses the card when the CPAP machine is idle (no therapy recording)
- **Short Sessions:** Limits exclusive access time (default 5 minutes) to ensure CPAP can reclaim access if needed
- **Automatic Release:** Releases control immediately after session or if therapy starts

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

## Web Interface (Optional)

The firmware includes an optional web server for monitoring and configuration.

### Accessing the Web Interface

1. After device connects to WiFi, note the IP address from serial monitor
2. Open browser and go to: `http://<device-ip>/`

### Available Features

**Status Page** (`http://<device-ip>/`)
- Auto-refreshes every 5 seconds
- System uptime and current time
- WiFi signal strength (color-coded)
- Next scheduled upload time
- Time budget remaining
- Pending files count
- Current configuration

**Trigger Upload** (`http://<device-ip>/trigger-upload`)
- Force immediate upload (bypasses schedule)
- Useful for testing without waiting

**SD Activity Monitor** (`http://<device-ip>/monitor`)
- Real-time graph of SD card read/write activity
- Useful for diagnosing CPAP machine interference
- Helps verify "Smart Mode" inactivity detection

**View Status** (`http://<device-ip>/status`)
- JSON format system status
- Useful for monitoring

**Reset State** (`http://<device-ip>/reset-state`)
- Clears upload history
- Forces re-upload of all files
- Useful for testing from clean state

**View Configuration** (`http://<device-ip>/config`)
- Shows current config.txt values
- Useful for verifying configuration

**View Logs** (`http://<device-ip>/logs`)
- Shows recent log messages from circular buffer
- Useful for troubleshooting

**Firmware Update** (`http://<device-ip>/ota`) - OTA firmware only
- Upload new firmware via web browser
- Download firmware from URL
- Real-time progress monitoring
- Automatic device restart after update
- **⚠️ Important:** Ensure stable WiFi and do not power off during updates

### Security Warning
⚠️ The web server has no authentication. Only use on trusted networks.

---

## Troubleshooting

### Firmware Upload Issues

**"Python is not installed" (Windows)**
- Download Python from https://python.org
- During installation, check "Add Python to PATH"
- Restart command prompt after installation

**"Failed to connect to ESP32"**
- Verify SD WIFI PRO is connected to development board
- Check switches: Switch 1 OFF, Switch 2 ON
- Check USB cable (must be data cable, not charge-only)
- Try different USB port
- Verify correct serial port selected
- Try holding the BOOT button during upload

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

### ⚠️ SD Card Errors — Use Scheduled Mode

> **If your CPAP machine is showing "SD Card Error" or "SD Card Removed" messages, switch to `UPLOAD_MODE = scheduled` immediately.**

The default `smart` upload mode detects SD bus activity to decide when it is safe to take the card. On some CPAP models or firmware versions the activity detection may not work correctly, causing the uploader to take the SD card at the wrong moment. This results in your CPAP displaying an SD card error.

**Fix:**

```ini
UPLOAD_MODE = scheduled
UPLOAD_START_HOUR = 9
UPLOAD_END_HOUR = 23
```

In scheduled mode the uploader **only runs during the configured window** (e.g. 9 AM–11 PM) and completely avoids uploading while you sleep. This eliminates any possibility of conflicting with your CPAP at night.

> **Tip:** Set `UPLOAD_START_HOUR` and `UPLOAD_END_HOUR` to cover the hours when you are typically awake and not using the CPAP machine.

---

### SD Card Issues

**SD card not detected**
- The SD WIFI PRO is an integrated SD card with ESP32 chip
- Verify the device is properly inserted into the CPAP machine
- Check that the device is receiving power

**config.txt not found**
- Ensure file is named exactly `config.txt` (lowercase)
- Place file in root of SD card (not in a folder)
- Verify file uses correct Key-Value format (see examples)

### Cloud / SleepHQ Issues

**Authentication Failed**
- Verify `CLOUD_CLIENT_ID` and `CLOUD_CLIENT_SECRET` in `config.txt`
- Ensure no extra spaces or hidden characters in the credentials
- Check logs for "401 Unauthorized" or "403 Forbidden" errors

**Upload Failed**
- Check internet connection (Cloud upload requires internet)
- Verify `GMT_OFFSET_HOURS` is correct (timestamps matter)
- View logs for specific API error messages

**Frequent Reboots**
- Normal: Automatic heap recovery reboots when memory fragmented
- Check logs for "Heap fragmented" messages
- Reboots are seamless and preserve upload state
- If excessive, check for large file uploads or mixed backend usage

**Nothing Uploads**
- Check if files have actually changed (pre-flight scan skips unchanged files)
- Verify recent completed folders have file size changes
- Check logs for "nothing to upload — skipping" messages

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
- For scheduled mode: check current local time is inside `UPLOAD_START_HOUR`-`UPLOAD_END_HOUR`
- For smart mode: verify therapy has ended and inactivity threshold elapsed
- Verify internet connection for time sync
- Check SMB connection is working
- View logs via web interface: `http://<device-ip>/logs`

**Upload incomplete**
- Increase `EXCLUSIVE_ACCESS_MINUTES` if files are large
- Check available space on network share
- Verify network stability
- Check logs for specific errors

**Same files uploading repeatedly**
- Check `.upload_state.v2` exists on SD card
- Check `.upload_state.v2.log` is writable
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

Use the included monitor scripts for easy serial monitoring:

**Windows:**
```cmd
monitor.bat
```

**Linux/Mac:**
```bash
./monitor.sh
```

These scripts automatically:
- Activate the Python virtual environment
- Check for PlatformIO installation
- Start the serial monitor on the correct port

**Manual monitoring (alternative):**
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
- Look for `.upload_state.v2.smb`/`.cloud` and their `.log` files on SD card
- Contains upload history, retry counts, and incremental journal updates for each backend

---

## File Structure

### On SD Card
```
/
├── config.txt               # Your configuration (you create this)
├── .upload_state.v2.smb     # SMB upload tracking (auto-created)
├── .upload_state.v2.smb.log # SMB upload journal (auto-created)
├── .upload_state.v2.cloud   # Cloud upload tracking (auto-created)
├── .upload_state.v2.cloud.log # Cloud upload journal (auto-created)
├── Identification.json      # ResMed 11 identification (if present)
├── Identification.crc       # Identification checksum (if present)
├── Identification.tgt       # ResMed 9/10 identification (if present)
├── STR.edf                  # Summary data (if present)
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
├── Identification.json
├── Identification.crc
├── Identification.tgt
├── STR.edf
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
REM Install esptool
pip install esptool

REM OTA firmware
python -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 firmware-ota.bin

REM Standard firmware
python -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 firmware-standard.bin
```

### macOS/Linux
```bash
# Install esptool
pip install esptool

# OTA firmware
python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 firmware-ota.bin

# Standard firmware
python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 firmware-standard.bin
```

**Note:** The firmware files are complete images (bootloader + partitions + app) and must be flashed at address `0x0`.

---

## Package Contents

**Firmware Binaries:**
- `firmware-ota.bin` - Complete OTA firmware for initial flashing (1.3MB)
- `firmware-ota-upgrade.bin` - App-only binary for web OTA updates (1.2MB)
- `firmware-standard.bin` - Complete standard firmware, can be re-flashed (1.2MB)

**Upload Scripts:**
- `upload-ota.bat` - Windows OTA firmware upload script
- `upload-standard.bat` - Windows standard firmware upload script
- `upload.sh` - macOS/Linux upload script (supports both firmware types)

**Monitor Scripts:**
- `monitor.bat` - Windows serial monitor script
- `monitor.sh` - macOS/Linux serial monitor script

**Configuration Examples:**
- `config.txt.example.simple` - Minimal configuration (bare essentials)
- `config.txt.example.smb` - SMB/network share configuration
- `config.txt.example.sleephq` - SleepHQ cloud configuration
- `config.txt.example.both` - Dual upload (SMB + SleepHQ)

**Documentation:**
- `README.md` - This file
- `requirements.txt` - Python dependencies (esptool)

---

## Legal & Trademarks

- **SleepHQ** is a trademark of its respective owner. This project is an unofficial client and is not affiliated with, endorsed by, or associated with SleepHQ.
  - This project uses the officially published [SleepHQ API](https://sleephq.com/api-docs) and does not rely on any non-official methods.
  - This project is **not intended to compete** with the official [Magic Uploader](https://shop.sleephq.com/products/magic-uploader-pro). We strongly encourage users to support the platform by purchasing the official solution, which comes with vendor support and requires no technical setup (flashing).
- **ResMed** is a trademark of ResMed. This software is not affiliated with ResMed.
- All other trademarks are the property of their respective owners.

### Disclaimer & No Warranty

**USE AT YOUR OWN RISK.**

This project (including source code, pre-compiled binaries, and documentation) is provided "as is" and **without any warranty of any kind**, express or implied.

**By using this software, you acknowledge and agree that:**
1.  **You are solely responsible** for the safety and operation of your CPAP machine and data.
2.  The authors and contributors **guarantee nothing** regarding the reliability, safety, or suitability of this software.
3.  **We are not liable** for any damage to your CPAP machine, SD card, loss of therapy data, or any other direct or indirect damage resulting from the use of this project.
4.  **Warranty Implication:** Using third-party accessories or software with your medical device may void its warranty. You accept this risk entirely.

This software interacts directly with medical device hardware and file systems. While every effort has been made to ensure safety, bugs or hardware incompatibilities can occur.

**GPL-3.0 License Disclaimer:**
> THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

<details>
<summary><b>⚠️ Upgrading from v0.7.x or earlier? (Breaking change in v0.8.0)</b></summary>

The configuration format changed from `config.json` (JSON) to `config.txt` (key-value, one setting per line).

Your existing `config.json` **will not** work. You must create a new `config.txt`:

1. Delete or rename your old `config.json`
2. Create a new `config.txt` using the examples in this package (`config.txt.example*`)
3. Copy your settings manually using the format: `SETTING_NAME = value` (one setting per line)

The new format is simpler and has no JSON syntax to get wrong.
</details>

## Support

For issues, questions, or contributions, visit the project repository.

**Hardware:** ESP32-PICO-D4 (SD WIFI PRO)  

**Requirements:**
- Python 3.7+ (https://python.org)
- USB cable (data cable, not charge-only)
- SD WIFI PRO development board for initial flashing


