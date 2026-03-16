// Written for Grafana Labs to demonstrate how to use the M5StickC Plus2 with Grafana Cloud
// RFID2 Unit reads NTAG215 NFC stickers and triggers a remote Watering unit via ESP-NOW.
// Scan events are pushed to Grafana Cloud as Influx line-protocol metrics.
//
// Tags can be:
//   - "Golden tickets" identified by UID  -> longer watering + special Grafana metric
//   - Plant tags with NDEF text records   -> plant name sent as Grafana label
//   - Blank/unknown tags                  -> identified by UID only
//
// Write plant names to tags using a phone NFC app (e.g. "NFC Tools"):
//   Add Record > Text > type the plant name (e.g. "Rosemary") > Write
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

#include "config.h"

MFRC522_I2C mfrc522(0x28, -1);

struct WaterCommand {
    uint8_t command;       // 1 = start pump
    uint8_t duration_sec;
};

struct MetricPayload {
    char tag_uid[22];
    char plant[32];
    bool golden;
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static unsigned long lastScanTime = 0;
static const unsigned long SCAN_COOLDOWN_MS = 3000;

static const char* goldenUIDs[] = { GOLDEN_UID_1, GOLDEN_UID_2, GOLDEN_UID_3 };
static const int   NUM_GOLDEN   = 3;

// ──────────────────────────────────────────────
// ESP-NOW
// ──────────────────────────────────────────────

static uint8_t peerMAC[] = WATERING_MAC;
static bool espNowReady = false;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void initESPNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_send_cb(onDataSent);

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
                // Influx line protocol with plant name as a tag
                String plant = String(msg.plant);
                if (plant.length() == 0) plant = "unknown";

                // Escape spaces in plant name for Influx tag values
                plant.replace(" ", "\\ ");

                String postData = "m5RFID,location=home,plant=" + plant
                    + " tag_uid=\"" + String(msg.tag_uid) + "\""
                    + ",scan=1"
                    + ",golden=" + String(msg.golden ? 1 : 0);

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

bool isGoldenTicket(const String &uid) {
    for (int i = 0; i < NUM_GOLDEN; i++) {
        if (strlen(goldenUIDs[i]) > 0 && uid.equalsIgnoreCase(goldenUIDs[i])) {
            return true;
        }
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
// Golden ticket LCD animation
// ──────────────────────────────────────────────

void showGoldenTicket(const String &uid) {
    for (int flash = 0; flash < 3; flash++) {
        StickCP2.Display.fillScreen(YELLOW);
        delay(120);
        StickCP2.Display.fillScreen(BLACK);
        delay(80);
    }
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(YELLOW);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(20, 5);
    StickCP2.Display.println("GOLDEN");
    StickCP2.Display.setCursor(20, 28);
    StickCP2.Display.println("TICKET!");

    StickCP2.Display.setTextColor(WHITE);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 60);
    StickCP2.Display.printf("Watering %ds", GOLDEN_DURATION_SEC);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setCursor(10, 80);
    StickCP2.Display.print("UID:");
    StickCP2.Display.println(uid);
}

// ──────────────────────────────────────────────
// Normal plant tag LCD
// ──────────────────────────────────────────────

void showPlantTag(const String &uid, const String &plant, uint8_t duration) {
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 5);
    StickCP2.Display.println("Plant Tag");

    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 30);
    if (plant.length() > 0) {
        StickCP2.Display.println(plant);
    } else {
        StickCP2.Display.println("(no name)");
    }

    StickCP2.Display.setTextColor(YELLOW);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 60);
    StickCP2.Display.printf("Watering %ds...", duration);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setCursor(10, 80);
    StickCP2.Display.print("UID:");
    StickCP2.Display.println(uid);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setCursor(10, 100);
    StickCP2.Display.println("Sent to Grafana + pump!");
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
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
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

    StickCP2.Display.fillScreen(BLACK);
    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("RFID Reader");
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("Ready - tap NFC tag");
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();

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

    bool golden = isGoldenTicket(uidHex);
    String plant = "";
    uint8_t duration;

    if (golden) {
        duration = GOLDEN_DURATION_SEC;
        plant = "GoldenTicket";
        Serial.println("*** GOLDEN TICKET! ***");
    } else {
        duration = WATER_DURATION_SEC;
        plant = readNDEFText();
        if (plant.length() > 0) {
            Serial.print("Plant name: ");
            Serial.println(plant);
        } else {
            Serial.println("No NDEF text found (blank tag)");
        }
    }

    // --- Update LCD ---
    if (golden) {
        showGoldenTicket(uidHex);
    } else {
        showPlantTag(uidHex, plant, duration);
    }

    // --- Send ESP-NOW command ---
    sendWaterCommand(duration);

    // --- Queue metric for Grafana Cloud ---
    MetricPayload payload = {};
    uidHex.toCharArray(payload.tag_uid, sizeof(payload.tag_uid));
    plant.toCharArray(payload.plant, sizeof(payload.plant));
    payload.golden = golden;
    if (xQueueSend(httpQueue, &payload, 0) != pdPASS) {
        Serial.println("HTTP queue full, dropping metric");
    }

    mfrc522.PICC_HaltA();
    delay(200);
}
