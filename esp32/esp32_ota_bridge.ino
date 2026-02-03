/*
 * esp32_ota_bridge.ino - ESP32 OTA WiFi Bridge
 * 
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 * 
 * Role: Downloads firmware updates from server via WiFi/HTTP,
 *       forwards data to STM32 via UART with chunked transfer protocol.
 * 
 * Hardware:
 *   ESP32 DevKit V1
 *   GPIO17 (TX2) -> STM32 PA3 (USART2_RX)
 *   GPIO16 (RX2) <- STM32 PA2 (USART2_TX)
 *   Common GND
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"

// ─────────────────────────────────────────────
// Global Variables
// ─────────────────────────────────────────────

HardwareSerial STM32Serial(2);  // UART2 for STM32 communication

// Device state
String currentDeviceVersion = "1.0.0";  // Will be queried from STM32
bool otaInProgress = false;
unsigned long lastCheckTime = 0;

// Transfer statistics
uint32_t totalBytesSent = 0;
uint32_t totalChunks = 0;
uint32_t failedChunks = 0;


// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────

void setup() {
    // Debug serial (USB)
    DEBUG_SERIAL.begin(DEBUG_BAUD);
    delay(1000);
    
    DBG_PRINTLN("\n========================================");
    DBG_PRINTLN("  ESP32 OTA WiFi Bridge");
    DBG_PRINTLN("  CDAC ACTS DESD Project");
    DBG_PRINTLN("========================================\n");
    
    // LED setup
    pinMode(LED_WIFI, OUTPUT);
    pinMode(LED_OTA_ACTIVE, OUTPUT);
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_OTA_ACTIVE, LOW);
    
    // STM32 UART
    STM32Serial.begin(STM32_UART_BAUD, SERIAL_8N1, STM32_UART_RX, STM32_UART_TX);
    DBG_PRINTF("[UART] Initialized: %d baud, TX=GPIO%d, RX=GPIO%d\n", 
               STM32_UART_BAUD, STM32_UART_TX, STM32_UART_RX);
    
    // Connect WiFi
    connectWiFi();
    
    // Query STM32 for current version
    querySTM32Version();
    
    DBG_PRINTLN("[INIT] Setup complete. Entering main loop.\n");
}


// ─────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────

void loop() {
    // Maintain WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        DBG_PRINTLN("[WIFI] Connection lost! Reconnecting...");
        digitalWrite(LED_WIFI, LOW);
        connectWiFi();
    }
    
    // Periodic update check
    if (!otaInProgress && (millis() - lastCheckTime > OTA_CHECK_INTERVAL)) {
        lastCheckTime = millis();
        checkForUpdates();
    }
    
    // Handle incoming STM32 messages
    handleSTM32Messages();
    
    delay(100);
}


// ─────────────────────────────────────────────
// WiFi Management
// ─────────────────────────────────────────────

void connectWiFi() {
    DBG_PRINTF("[WIFI] Connecting to: %s", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT) {
            DBG_PRINTLN("\n[WIFI] Connection timeout! Retrying...");
            WiFi.disconnect();
            delay(1000);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            startTime = millis();
        }
        DBG_PRINT(".");
        digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));  // Blink during connect
        delay(500);
    }
    
    digitalWrite(LED_WIFI, HIGH);  // Solid = connected
    DBG_PRINTLN();
    DBG_PRINTF("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    DBG_PRINTF("[WIFI] Signal strength: %d dBm\n", WiFi.RSSI());
}


// ─────────────────────────────────────────────
// Server Communication
// ─────────────────────────────────────────────

void checkForUpdates() {
    DBG_PRINTLN("[OTA] Checking server for updates...");
    
    HTTPClient http;
    String url = String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + OTA_VERSION_URL;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DBG_PRINTF("[OTA] Server response: %s\n", payload.c_str());
        
        // Parse JSON response (simple parsing)
        String serverVersion = parseJsonString(payload, "version");
        String serverHash = parseJsonString(payload, "sha256");
        int serverSize = parseJsonInt(payload, "size");
        
        DBG_PRINTF("[OTA] Server version: %s, Device version: %s\n", 
                   serverVersion.c_str(), currentDeviceVersion.c_str());
        
        if (serverVersion.length() > 0 && serverVersion != currentDeviceVersion) {
            DBG_PRINTLN("[OTA] *** New firmware available! Starting update... ***");
            startOTAUpdate(serverVersion, serverHash, serverSize);
        } else {
            DBG_PRINTLN("[OTA] Firmware is up to date.");
        }
    } else {
        DBG_PRINTF("[OTA] Server check failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
}


void startOTAUpdate(String newVersion, String expectedHash, int fwSize) {
    otaInProgress = true;
    digitalWrite(LED_OTA_ACTIVE, HIGH);
    totalBytesSent = 0;
    totalChunks = 0;
    failedChunks = 0;
    
    DBG_PRINTF("\n[OTA] === Starting OTA Update ===\n");
    DBG_PRINTF("[OTA] Current: v%s -> Target: v%s\n", 
               currentDeviceVersion.c_str(), newVersion.c_str());
    DBG_PRINTF("[OTA] Expected hash: %s\n", expectedHash.c_str());
    
    // Try delta update first, fall back to full
    bool success = downloadAndSendDelta(newVersion);
    
    if (!success) {
        DBG_PRINTLN("[OTA] Delta failed, trying full firmware download...");
        success = downloadAndSendFull(newVersion);
    }
    
    if (success) {
        DBG_PRINTLN("[OTA] === Update transfer complete! ===");
        DBG_PRINTF("[OTA] Bytes sent: %u, Chunks: %u, Failed: %u\n",
                   totalBytesSent, totalChunks, failedChunks);
        currentDeviceVersion = newVersion;
    } else {
        DBG_PRINTLN("[OTA] !!! Update FAILED !!!");
        sendCommandToSTM32(CMD_ABORT);
    }
    
    otaInProgress = false;
    digitalWrite(LED_OTA_ACTIVE, LOW);
}


bool downloadAndSendDelta(String targetVersion) {
    DBG_PRINTLN("[OTA] Requesting delta patch from server...");
    
    HTTPClient http;
    String url = String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + OTA_DELTA_URL;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String body = "{\"current_version\":\"" + currentDeviceVersion + 
                  "\",\"target_version\":\"" + targetVersion + "\"}";
    
    int httpCode = http.POST(body);
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        DBG_PRINTF("[OTA] Delta patch size: %d bytes\n", contentLength);
        
        // Signal STM32: OTA starting (delta mode)
        if (!sendStartOTA(contentLength, true)) {
            http.end();
            return false;
        }
        
        // Stream data to STM32
        WiFiClient* stream = http.getStreamPtr();
        bool success = streamToSTM32(stream, contentLength);
        
        http.end();
        
        if (success) {
            return sendEndOTA();
        }
    } else {
        DBG_PRINTF("[OTA] Delta request failed: %d\n", httpCode);
    }
    
    http.end();
    return false;
}


bool downloadAndSendFull(String targetVersion) {
    DBG_PRINTLN("[OTA] Downloading full firmware...");
    
    HTTPClient http;
    String url = String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + OTA_FULL_FW_URL;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        DBG_PRINTF("[OTA] Full firmware size: %d bytes\n", contentLength);
        
        // Signal STM32: OTA starting (full mode)
        if (!sendStartOTA(contentLength, false)) {
            http.end();
            return false;
        }
        
        WiFiClient* stream = http.getStreamPtr();
        bool success = streamToSTM32(stream, contentLength);
        
        http.end();
        
        if (success) {
            return sendEndOTA();
        }
    } else {
        DBG_PRINTF("[OTA] Full download failed: %d\n", httpCode);
    }
    
    http.end();
    return false;
}


// ─────────────────────────────────────────────
// UART Transfer Protocol
// ─────────────────────────────────────────────

bool sendStartOTA(uint32_t totalSize, bool isDelta) {
    /*
     * START_OTA packet:
     * [CMD_START_OTA] [isDelta:1] [totalSize:4] [checksum:1]
     */
    uint8_t packet[7];
    packet[0] = CMD_START_OTA;
    packet[1] = isDelta ? 0x01 : 0x00;
    packet[2] = (totalSize >> 0)  & 0xFF;
    packet[3] = (totalSize >> 8)  & 0xFF;
    packet[4] = (totalSize >> 16) & 0xFF;
    packet[5] = (totalSize >> 24) & 0xFF;
    packet[6] = computeChecksum(packet, 6);
    
    DBG_PRINTF("[UART] Sending START_OTA: size=%u, delta=%d\n", totalSize, isDelta);
    STM32Serial.write(packet, 7);
    STM32Serial.flush();
    
    return waitForACK("START_OTA");
}


bool streamToSTM32(WiFiClient* stream, int totalSize) {
    uint8_t buffer[CHUNK_SIZE];
    int remaining = totalSize;
    int chunkIndex = 0;
    
    while (remaining > 0 && stream->connected()) {
        // Read chunk from HTTP stream
        int toRead = min(remaining, (int)CHUNK_SIZE);
        int bytesRead = 0;
        
        unsigned long readStart = millis();
        while (bytesRead < toRead && (millis() - readStart < 10000)) {
            if (stream->available()) {
                int r = stream->read(buffer + bytesRead, toRead - bytesRead);
                if (r > 0) bytesRead += r;
            }
            delay(1);
        }
        
        if (bytesRead == 0) {
            DBG_PRINTLN("[OTA] Stream read timeout!");
            return false;
        }
        
        // Send chunk to STM32 with retry
        bool chunkSent = false;
        for (int retry = 0; retry < MAX_RETRIES; retry++) {
            if (sendChunkToSTM32(buffer, bytesRead, chunkIndex)) {
                chunkSent = true;
                break;
            }
            DBG_PRINTF("[UART] Chunk %d retry %d/%d\n", chunkIndex, retry + 1, MAX_RETRIES);
            failedChunks++;
        }
        
        if (!chunkSent) {
            DBG_PRINTF("[UART] Chunk %d failed after %d retries!\n", chunkIndex, MAX_RETRIES);
            return false;
        }
        
        remaining -= bytesRead;
        totalBytesSent += bytesRead;
        totalChunks++;
        chunkIndex++;
        
        // Progress update every 10 chunks
        if (chunkIndex % 10 == 0) {
            int progress = ((totalSize - remaining) * 100) / totalSize;
            DBG_PRINTF("[OTA] Progress: %d%% (%d/%d bytes)\n", 
                       progress, totalSize - remaining, totalSize);
        }
        
        delay(INTER_CHUNK_DELAY);
    }
    
    return remaining == 0;
}


bool sendChunkToSTM32(uint8_t* data, int length, int chunkIndex) {
    /*
     * CHUNK packet:
     * [CMD_CHUNK_DATA] [chunkIndex:2] [length:2] [data:N] [checksum:1]
     */
    
    // Header
    STM32Serial.write(CMD_CHUNK_DATA);
    STM32Serial.write((uint8_t)(chunkIndex & 0xFF));
    STM32Serial.write((uint8_t)((chunkIndex >> 8) & 0xFF));
    STM32Serial.write((uint8_t)(length & 0xFF));
    STM32Serial.write((uint8_t)((length >> 8) & 0xFF));
    
    // Data
    STM32Serial.write(data, length);
    
    // Checksum (XOR of all bytes including header)
    uint8_t checksum = CMD_CHUNK_DATA;
    checksum ^= (chunkIndex & 0xFF);
    checksum ^= ((chunkIndex >> 8) & 0xFF);
    checksum ^= (length & 0xFF);
    checksum ^= ((length >> 8) & 0xFF);
    for (int i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    STM32Serial.write(checksum);
    STM32Serial.flush();
    
    return waitForACK("CHUNK");
}


bool sendEndOTA() {
    /*
     * END_OTA packet:
     * [CMD_END_OTA] [checksum:1]
     */
    uint8_t packet[2];
    packet[0] = CMD_END_OTA;
    packet[1] = CMD_END_OTA;  // Checksum = command itself
    
    DBG_PRINTLN("[UART] Sending END_OTA");
    STM32Serial.write(packet, 2);
    STM32Serial.flush();
    
    return waitForACK("END_OTA");
}


void sendCommandToSTM32(uint8_t cmd) {
    STM32Serial.write(cmd);
    STM32Serial.flush();
}


bool waitForACK(const char* context) {
    unsigned long startTime = millis();
    
    while (millis() - startTime < ACK_TIMEOUT) {
        if (STM32Serial.available()) {
            uint8_t response = STM32Serial.read();
            if (response == CMD_ACK) {
                return true;
            } else if (response == CMD_NACK) {
                DBG_PRINTF("[UART] NACK received for %s\n", context);
                return false;
            }
        }
        delay(1);
    }
    
    DBG_PRINTF("[UART] ACK timeout for %s\n", context);
    return false;
}


uint8_t computeChecksum(uint8_t* data, int length) {
    uint8_t checksum = 0;
    for (int i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}


// ─────────────────────────────────────────────
// STM32 Communication
// ─────────────────────────────────────────────

void querySTM32Version() {
    DBG_PRINTLN("[UART] Querying STM32 firmware version...");
    
    sendCommandToSTM32(CMD_VERSION_REQ);
    
    unsigned long startTime = millis();
    String version = "";
    
    while (millis() - startTime < 3000) {
        if (STM32Serial.available()) {
            uint8_t b = STM32Serial.read();
            if (b == CMD_VERSION_RESP) {
                // Read version string (null-terminated)
                while (millis() - startTime < 3000) {
                    if (STM32Serial.available()) {
                        char c = STM32Serial.read();
                        if (c == '\0') break;
                        version += c;
                    }
                }
                break;
            }
        }
        delay(10);
    }
    
    if (version.length() > 0) {
        currentDeviceVersion = version;
        DBG_PRINTF("[UART] STM32 firmware version: %s\n", currentDeviceVersion.c_str());
    } else {
        DBG_PRINTLN("[UART] No version response from STM32, using default");
    }
}


void handleSTM32Messages() {
    while (STM32Serial.available()) {
        uint8_t byte = STM32Serial.read();
        // Handle unsolicited messages from STM32 if needed
        DBG_PRINTF("[UART] Received from STM32: 0x%02X\n", byte);
    }
}


// ─────────────────────────────────────────────
// JSON Parsing Helpers (lightweight, no library)
// ─────────────────────────────────────────────

String parseJsonString(String json, String key) {
    String searchKey = "\"" + key + "\":\"";
    int startIdx = json.indexOf(searchKey);
    if (startIdx == -1) {
        searchKey = "\"" + key + "\": \"";
        startIdx = json.indexOf(searchKey);
    }
    if (startIdx == -1) return "";
    
    startIdx += searchKey.length();
    int endIdx = json.indexOf("\"", startIdx);
    if (endIdx == -1) return "";
    
    return json.substring(startIdx, endIdx);
}


int parseJsonInt(String json, String key) {
    String searchKey = "\"" + key + "\":";
    int startIdx = json.indexOf(searchKey);
    if (startIdx == -1) {
        searchKey = "\"" + key + "\": ";
        startIdx = json.indexOf(searchKey);
    }
    if (startIdx == -1) return 0;
    
    startIdx += searchKey.length();
    // Skip whitespace
    while (startIdx < (int)json.length() && json.charAt(startIdx) == ' ') startIdx++;
    
    String numStr = "";
    while (startIdx < (int)json.length() && json.charAt(startIdx) >= '0' && json.charAt(startIdx) <= '9') {
        numStr += json.charAt(startIdx);
        startIdx++;
    }
    
    return numStr.toInt();
}
