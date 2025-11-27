#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

/*
 * Pin Configuration for SD-WIFI-PRO (ESP32-PICO-D4)
 * 
 * This board features:
 * - ESP32-PICO-D4 MCU
 * - 4MB built-in Flash
 * - SD card interface (SDIO 4-bit mode)
 * - Shared SD card access with CPAP machine
 */

// ============================================================================
// SD Card Pins (SDIO 4-bit mode)
// ============================================================================
// The SD card uses SDIO interface for high-speed access
#define SD_CMD_PIN    15    // SDIO Command pin
#define SD_CLK_PIN    14    // SDIO Clock pin
#define SD_D0_PIN     2     // SDIO Data 0
#define SD_D1_PIN     4     // SDIO Data 1
#define SD_D2_PIN     12    // SDIO Data 2
#define SD_D3_PIN     13    // SDIO Data 3 / CS pin for SPI mode

// Alternative SPI mode pin names (same physical pins)
#define SD_CS_PIN     13    // Chip Select (same as D3)
#define SD_MISO_PIN   2     // MISO (same as D0)
#define SD_MOSI_PIN   15    // MOSI (same as CMD)
#define SD_SCLK_PIN   14    // Clock (same as CLK)

// ============================================================================
// SD Card Control Pins
// ============================================================================
// These pins control SD card sharing between ESP32 and CPAP machine
#define SD_SWITCH_PIN 26    // SD card bus switch control (LOW = ESP has control)
#define SD_POWER_PIN  27    // SD card power control (if available)
#define CS_SENSE      32    // Chip Select sense - detects when CPAP machine accesses SD, TODO validate it is usable

// ============================================================================
// Configuration Button
// ============================================================================
#define AP_ENABLE_BUTTON -1  // Button to enable AP mode during startup (-1 to disable)

// ============================================================================
// SD Card Configuration
// ============================================================================
#define SDIO_BIT_MODE_FAST     false      // Use 4-bit SDIO mode for faster access
#define SDIO_BIT_MODE_SLOW     true      // Use 1-bit SDIO mode for faster access
#define SPI_BLOCKOUT_PERIOD 10UL      // Seconds to block SD access after CPAP machine uses it

// SD card sharing control values
#define SD_SWITCH_ESP_VALUE   LOW     // Value to give ESP control of SD card
#define SD_SWITCH_CPAP_VALUE HIGH     // Value to give CPAP machine control of SD card

#endif // PINS_CONFIG_H