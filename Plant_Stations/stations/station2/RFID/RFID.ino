// Written for Grafana Labs to demonstrate how to use the M5StickC Plus2 with Grafana Cloud
// RFID2 Unit reads NTAG215 NFC stickers and triggers a remote Watering unit via ESP-NOW.
// Scan events are pushed to Grafana Cloud as Influx line-protocol metrics.
//
// Tags can be:
//   - "Golden stickers" with NDEF text starting with "GOLDEN:" (e.g. "GOLDEN:Free T-Shirt")
//   - Blank/normal tags -> trigger watering, plant name from config
//
// Write golden stickers using a phone NFC app (e.g. "NFC Tools"):
//   Add Record > Text > type "GOLDEN:Your Prize Here" > Write
//
// Claimed golden stickers are persisted in NVS (flash) so they survive reboots.
// Hold Button A during boot to clear the claimed list for a new event day.
//
// Hardware: M5StickC Plus2 + Unit RFID2 (MFRC522, I2C 0x28) on Grove Port A
// Platform: Arduino M5Stack Board Manager
// Dependent Libraries:
//   M5StickCPlus2: https://github.com/m5stack/M5StickCPlus2
//   MFRC522_I2C (>= 1.0): Arduino Library Manager

#include <M5StickCPlus2.h>
#include <Wire.h>
#include <MFRC522_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <Preferences.h>

#include "config.h"

MFRC522_I2C mfrc522(0x28, -1);
Preferences prefs;

static const int MAX_GOLDEN = 20;

struct WaterCommand {
    uint8_t command;       // 1 = start pump
    uint8_t duration_sec;
};

struct MetricPayload {
    char tag_uid[22];
    char prize[32];
    bool golden;
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static int winnerCount = 0;
static String claimedUIDs[MAX_GOLDEN];
static int    claimedCount = 0;

static unsigned long lastScanTime = 0;
static const unsigned long SCAN_COOLDOWN_MS = 3000;

static unsigned long displayMessageTime = 0;
static unsigned long displayHoldTime    = 0;
static bool          displayShowingMessage = false;
static const unsigned long GOLDEN_DISPLAY_MS = 30000;
static const unsigned long NORMAL_DISPLAY_MS = 8000;

// ──────────────────────────────────────────────
// ESP-NOW
// ──────────────────────────────────────────────

static uint8_t peerMAC[] = WATERING_MAC;
static bool espNowReady = false;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataSentV3(const esp_now_send_info_t *info, esp_now_send_status_t status) {
    onDataSent(info->des_addr, status);
}
#endif

void initESPNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_now_register_send_cb(onDataSentV3);
#else
    esp_now_register_send_cb(onDataSent);
#endif

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ESP-NOW add peer failed");
        return;
    }
    espNowReady = true;
    Serial.println("ESP-NOW ready");
}

void sendWaterCommand(uint8_t duration) {
    if (!espNowReady) return;
    WaterCommand cmd = { 1, duration };
    esp_now_send(peerMAC, (uint8_t *)&cmd, sizeof(cmd));
}

// ──────────────────────────────────────────────
// WiFi
// ──────────────────────────────────────────────

void initWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");
    int wifi_loop_count = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_loop_count += 1;
        if (wifi_loop_count > 40) {
            ESP.restart();
        }
    }
    Serial.println();
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi channel: ");
    Serial.println(WiFi.channel());
}

// ──────────────────────────────────────────────
// Async HTTP task (FreeRTOS)
// ──────────────────────────────────────────────

void sendHttpPost(void *parameter) {
    MetricPayload msg;

    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();

    HTTPClient http;
    String url = "https://" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write";

    Serial.print("Grafana URL: ");
    Serial.println(url);

    while (true) {
        if (xQueueReceive(httpQueue, &msg, portMAX_DELAY)) {
            if (WiFi.status() == WL_CONNECTED) {
                // Escape spaces in tag values for Influx line protocol
                String plant = String(PLANT_NAME);
                plant.replace(" ", "\\ ");
                String uid = String(msg.tag_uid);
                uid.replace(" ", "\\ ");

                // Every scan: m5RFID with plant + tag UID as labels
                String postData = "m5RFID,location=home,plant=" + plant
                    + ",tag_uid=" + uid
                    + " scan=1,golden=" + String(msg.golden ? 1 : 0);

                // Golden stickers: add a second line with separate measurement
                if (msg.golden) {
                    String prize = String(msg.prize);
                    if (prize.length() == 0) prize = "GoldenTicket";
                    prize.replace(" ", "\\ ");
                    postData += "\nm5GoldenTicket,location=home,plant=" + plant
                        + ",prize=" + prize
                        + " scan=1,winner_number=" + String(winnerCount);
                }

                Serial.print("POST: ");
                Serial.println(postData);

                http.begin(*client, url);
                http.addHeader("Content-Type", "text/plain");
                http.setAuthorization(GC_USER, GC_PASS);

                int rc = http.POST(postData);
                Serial.print("Grafana POST rc=");
                Serial.println(rc);

                http.end();
            }
        }
        delay(50);
    }
}

// ──────────────────────────────────────────────
// UID helpers
// ──────────────────────────────────────────────

String uidToHex(byte *uid, byte size) {
    String hex = "";
    for (byte i = 0; i < size; i++) {
        if (uid[i] < 0x10) hex += "0";
        hex += String(uid[i], HEX);
        if (i < size - 1) hex += ":";
    }
    hex.toUpperCase();
    return hex;
}

// ──────────────────────────────────────────────
// NVS persistence for claimed golden stickers
// ──────────────────────────────────────────────

void loadClaimedFromNVS() {
    prefs.begin("golden", true);
    claimedCount = prefs.getInt("count", 0);
    winnerCount  = claimedCount;
    for (int i = 0; i < claimedCount && i < MAX_GOLDEN; i++) {
        String key = "uid" + String(i);
        claimedUIDs[i] = prefs.getString(key.c_str(), "");
    }
    prefs.end();
    Serial.printf("Loaded %d claimed golden stickers from NVS\n", claimedCount);
}

void saveClaimedToNVS() {
    prefs.begin("golden", false);
    prefs.putInt("count", claimedCount);
    String key = "uid" + String(claimedCount - 1);
    prefs.putString(key.c_str(), claimedUIDs[claimedCount - 1]);
    prefs.end();
}

void clearClaimedNVS() {
    prefs.begin("golden", false);
    prefs.clear();
    prefs.end();
    claimedCount = 0;
    winnerCount  = 0;
    Serial.println("NVS claimed list cleared");
}

bool isAlreadyClaimed(const String &uid) {
    for (int i = 0; i < claimedCount; i++) {
        if (claimedUIDs[i].equalsIgnoreCase(uid)) return true;
    }
    return false;
}

// ──────────────────────────────────────────────
// NDEF text reader for NTAG215
//
// Reads raw pages from the tag starting at page 4 and extracts
// the text payload from the first NDEF Text record (type "T").
//
// Phone apps like "NFC Tools" write standard NDEF:
//   Page 4+: 03 <len> D1 01 <plen> 54 <status> <lang...> <text...> FE
// ──────────────────────────────────────────────

String readNDEFText() {
    byte buffer[18];
    byte bufSize;
    byte pages[32];
    int  totalRead = 0;

    // Read pages 4-11 (32 bytes) in two MIFARE_Read calls (each reads 16 bytes / 4 pages)
    for (byte startPage = 4; startPage <= 8; startPage += 4) {
        bufSize = sizeof(buffer);
        if (mfrc522.MIFARE_Read(startPage, buffer, &bufSize) != MFRC522_I2C::STATUS_OK) {
            Serial.printf("Failed to read page %d\n", startPage);
            return "";
        }
        int copyLen = min((int)(bufSize - 2), (int)(32 - totalRead));  // bufSize includes 2 CRC bytes
        memcpy(pages + totalRead, buffer, copyLen);
        totalRead += copyLen;
    }

    Serial.print("Raw NDEF bytes: ");
    for (int i = 0; i < totalRead; i++) Serial.printf("%02X ", pages[i]);
    Serial.println();

    // Find NDEF message TLV (type 0x03)
    int pos = 0;
    while (pos < totalRead) {
        if (pages[pos] == 0x03) break;     // NDEF message TLV
        if (pages[pos] == 0x00) { pos++; continue; }  // NULL TLV
        if (pages[pos] == 0xFE) return "";             // terminator, no NDEF
        // Other TLV: skip by length
        if (pos + 1 < totalRead) { pos += 2 + pages[pos + 1]; continue; }
        break;
    }
    if (pos >= totalRead || pages[pos] != 0x03) return "";

    pos++;  // skip TLV type
    if (pos >= totalRead) return "";
    int ndefLen = pages[pos++];  // TLV length

    // Parse NDEF record header
    if (pos >= totalRead) return "";
    byte header = pages[pos++];
    // byte tnf = header & 0x07;  // should be 0x01 (NFC Forum well-known type)
    bool sr = (header >> 4) & 1;  // short record flag

    if (pos >= totalRead) return "";
    byte typeLen = pages[pos++];

    if (pos >= totalRead) return "";
    int payloadLen;
    if (sr) {
        payloadLen = pages[pos++];
    } else {
        if (pos + 3 >= totalRead) return "";
        payloadLen = ((uint32_t)pages[pos] << 24) | ((uint32_t)pages[pos+1] << 16)
                   | ((uint32_t)pages[pos+2] << 8) | pages[pos+3];
        pos += 4;
    }

    // Check type is "T" (0x54) for text record
    if (pos >= totalRead) return "";
    if (typeLen != 1 || pages[pos] != 0x54) {
        Serial.println("NDEF record is not Text type");
        return "";
    }
    pos++;  // skip type byte

    // Parse text payload: status byte (language code length) + language + text
    if (pos >= totalRead) return "";
    byte status = pages[pos++];
    byte langLen = status & 0x3F;
    pos += langLen;  // skip language code (e.g. "en")

    int textLen = payloadLen - 1 - langLen;
    if (textLen <= 0 || pos + textLen > totalRead) return "";

    char text[32] = {0};
    int copyLen = min(textLen, 31);
    memcpy(text, pages + pos, copyLen);
    text[copyLen] = '\0';

    return String(text);
}

// ──────────────────────────────────────────────
// Golden sticker LCD animation
// ──────────────────────────────────────────────

void showGoldenSticker(const String &prize) {
    for (int flash = 0; flash < 5; flash++) {
        StickCP2.Display.fillScreen(YELLOW);
        delay(100);
        StickCP2.Display.fillScreen(BLACK);
        delay(60);
    }
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(YELLOW);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(15, 5);
    StickCP2.Display.println("YOU WON!");

    StickCP2.Display.setTextColor(WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 40);
    if (prize.length() > 0) {
        StickCP2.Display.println(prize);
    } else {
        StickCP2.Display.println("A prize!");
    }

    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 70);
    StickCP2.Display.println("Pick up at");
    StickCP2.Display.setCursor(10, 93);
    StickCP2.Display.println("the booth");
}

// ──────────────────────────────────────────────
// Normal plant tag LCD
// ──────────────────────────────────────────────

void showPlantTag(const String &plant, uint8_t duration) {
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("Watering!");

    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 40);
    if (plant.length() > 0) {
        StickCP2.Display.println(plant);
    } else {
        StickCP2.Display.println("Plant");
    }

    StickCP2.Display.setTextColor(YELLOW);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 70);
    StickCP2.Display.printf("%ds", duration);
}

void showIdleScreen() {
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("Tap to");
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("water!");

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 80);
    StickCP2.Display.println("Use NFC sticker");
}

// ──────────────────────────────────────────────
// Setup
// ──────────────────────────────────────────────

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    Serial.begin(115200);

    StickCP2.Display.setRotation(3);
    StickCP2.Display.fillScreen(BLACK);
    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("RFID Reader");

    // Hold Button A during boot to reset golden sticker claimed list
    StickCP2.update();
    if (StickCP2.BtnA.isPressed()) {
        clearClaimedNVS();
        StickCP2.Display.setTextColor(YELLOW);
        StickCP2.Display.setCursor(10, 40);
        StickCP2.Display.println("NVS CLEARED!");
        delay(2000);
    } else {
        loadClaimedFromNVS();
    }

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 60);
    StickCP2.Display.println("Connecting WiFi...");

    Wire.begin(32, 33);
    mfrc522.PCD_Init();
    Serial.println("MFRC522 initialized");

    MetricPayload d = {};
    httpQueue = xQueueCreate(5, sizeof(d));
    if (httpQueue == NULL) {
        Serial.println("Error creating queue!");
        return;
    }

    initWifi();
    initESPNow();

    xTaskCreate(sendHttpPost, "sendHttpPost", 8192, NULL, 1, &httpTaskHandle);

    showIdleScreen();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();

    // Return to idle screen after message timeout
    if (displayShowingMessage && millis() - displayMessageTime >= displayHoldTime) {
        displayShowingMessage = false;
        showIdleScreen();
    }

    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        delay(100);
        return;
    }

    if (millis() - lastScanTime < SCAN_COOLDOWN_MS) {
        mfrc522.PICC_HaltA();
        return;
    }
    lastScanTime = millis();

    String uidHex = uidToHex(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("Tag UID: ");
    Serial.println(uidHex);

    // Read NDEF text to detect golden stickers (prefix "GOLDEN:")
    String ndefText = readNDEFText();
    bool golden = ndefText.startsWith("GOLDEN:");
    uint8_t duration = WATER_DURATION_SEC;
    String prize = "";
    bool newWinner = false;

    if (golden) {
        prize = ndefText.substring(7);  // everything after "GOLDEN:"
        if (isAlreadyClaimed(uidHex)) {
            Serial.println("Golden sticker already claimed, watering only");
        } else if (claimedCount < MAX_GOLDEN) {
            claimedUIDs[claimedCount++] = uidHex;
            winnerCount++;
            newWinner = true;
            saveClaimedToNVS();
            Serial.printf("*** NEW GOLDEN STICKER! Winner #%d ***\n", winnerCount);
            Serial.println("Prize: " + prize);
        }
    } else {
        Serial.print("Normal tag on station: ");
        Serial.println(PLANT_NAME);
    }

    // --- Update LCD ---
    if (golden) {
        showGoldenSticker(prize);
    } else {
        showPlantTag(String(PLANT_NAME), duration);
    }
    displayShowingMessage = true;
    displayMessageTime = millis();
    displayHoldTime = golden ? GOLDEN_DISPLAY_MS : NORMAL_DISPLAY_MS;

    // --- Send ESP-NOW command ---
    sendWaterCommand(duration);

    // --- Queue metric for Grafana Cloud ---
    MetricPayload payload = {};
    uidHex.toCharArray(payload.tag_uid, sizeof(payload.tag_uid));
    prize.toCharArray(payload.prize, sizeof(payload.prize));
    payload.golden = newWinner;
    if (xQueueSend(httpQueue, &payload, 0) != pdPASS) {
        Serial.println("HTTP queue full, dropping metric");
    }

    mfrc522.PICC_HaltA();
    delay(200);
}
