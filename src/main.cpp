#include <Arduino.h>
#include "Config.h"
#include "SDCardManager.h"
#include "WiFiManager.h"
#include "FileUploader.h"

// ============================================================================
// Global Objects
// ============================================================================
Config config;
SDCardManager sdManager;
WiFiManager wifiManager;
FileUploader* uploader = nullptr;

// ============================================================================
// Setup Function
// ============================================================================
void setup() {
    // Initialize serial port
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== CPAP Data Auto-Uploader ===");

    // Initialize SD card control
    if (!sdManager.begin()) {
        Serial.println("Failed to initialize SD card manager");
        return;
    }

    // Take control of SD card
    Serial.println("Waiting to access SD card...");
    while (!sdManager.takeControl()) {
        delay(1000);
    }

    // Read config file from SD card
    Serial.println("Loading configuration...");
    if (!config.loadFromSD(sdManager.getFS())) {
        Serial.println("Failed to load configuration");
        sdManager.releaseControl();
        return;
    }

    Serial.println("Configuration loaded successfully");
    Serial.print("WiFi SSID: ");
    Serial.println(config.getWifiSSID());
    Serial.print("Endpoint: ");
    Serial.println(config.getEndpoint());

    // Release SD card back to CPAP machine
    sdManager.releaseControl();

    // Initialize WiFi in station mode
    if (!wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword())) {
        Serial.println("Failed to connect to WiFi");
        return;
    }

    // Initialize uploader
    uploader = new FileUploader(&config);

    Serial.println("Setup complete!");
}

// ============================================================================
// Loop Function
// ============================================================================
void loop() {
    // Check WiFi connection
    if (!wifiManager.isConnected()) {
        Serial.println("WiFi disconnected, reconnecting...");
        wifiManager.connectStation(config.getWifiSSID(), config.getWifiPassword());
        delay(5000);
        return;
    }

    // Try to take control of SD card
    if (sdManager.takeControl()) {
        // Handle new file detection and upload
        if (uploader) {
            uploader->uploadNewFiles(sdManager.getFS());
        }

        // Release SD card back to CPAP machine
        sdManager.releaseControl();
    } else {
        Serial.println("CPAP machine is using SD card, waiting...");
    }

    // Wait before next check
    delay(10000);  // Check every 10 seconds
}
