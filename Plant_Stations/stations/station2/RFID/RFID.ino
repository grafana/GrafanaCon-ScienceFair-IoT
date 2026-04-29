// Written for Grafana Labs to demonstrate how to use the M5StickC Plus2 with Grafana Cloud
// RFID2 Unit reads NTAG215 NFC stickers and triggers a remote Watering unit via ESP-NOW.
// Scan events are pushed to Grafana Cloud as Influx line-protocol metrics.
//
// Auto-write URL (optional, set NFC_WRITE_URL in config.h):
//   Every scanned sticker that does not already contain a URI record gets the
//   configured URL written to it automatically. The NDEF Text record (if any)
//   is preserved so golden sticker prizes still work. Stickers that already
//   have a URI are skipped (idempotent / safe to re-tap).
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

// ESP32 Arduino core 3.x changed the send-callback signature to take
// `const wifi_tx_info_t *` instead of `const uint8_t *mac_addr`.
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
#else
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
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
// NDEF reader for NTAG215
//
// Reads raw pages from the tag and iterates through all NDEF records.
// Returns the text payload of the first Text record (type "T" / 0x54),
// skipping over URI records (type "U" / 0x55) and anything else.
//
// Sets ndefHasUri = true if a URI record is found anywhere in the
// message, so the caller knows whether a URL still needs writing.
//
// Supports multi-record NDEF messages (e.g. URI + Text written
// via NFC Tools on a phone).
// ──────────────────────────────────────────────

static bool ndefHasUri = false;

String readNDEF() {
    ndefHasUri = false;

    byte buffer[18];
    byte bufSize;
    const int MAX_READ = 96;
    byte pages[MAX_READ];
    int  totalRead = 0;

    for (byte startPage = 4; startPage <= 24; startPage += 4) {
        bufSize = sizeof(buffer);
        if (mfrc522.MIFARE_Read(startPage, buffer, &bufSize) != MFRC522_I2C::STATUS_OK) {
            Serial.printf("Failed to read page %d\n", startPage);
            break;
        }
        int copyLen = min((int)(bufSize - 2), (int)(MAX_READ - totalRead));
        memcpy(pages + totalRead, buffer, copyLen);
        totalRead += copyLen;
    }

    Serial.print("Raw NDEF bytes: ");
    for (int i = 0; i < min(totalRead, 48); i++) Serial.printf("%02X ", pages[i]);
    if (totalRead > 48) Serial.print("...");
    Serial.println();

    // Find NDEF message TLV (type 0x03)
    int pos = 0;
    while (pos < totalRead) {
        if (pages[pos] == 0x03) break;
        if (pages[pos] == 0x00) { pos++; continue; }
        if (pages[pos] == 0xFE) return "";
        if (pos + 1 < totalRead) { pos += 2 + pages[pos + 1]; continue; }
        break;
    }
    if (pos >= totalRead || pages[pos] != 0x03) return "";

    pos++;  // skip TLV type byte
    if (pos >= totalRead) return "";

    // TLV length: single byte, or 3-byte form (FF hi lo) for messages > 254 bytes
    int ndefMsgLen;
    if (pages[pos] == 0xFF) {
        pos++;
        if (pos + 1 >= totalRead) return "";
        ndefMsgLen = ((int)pages[pos] << 8) | pages[pos + 1];
        pos += 2;
    } else {
        ndefMsgLen = pages[pos++];
    }
    int ndefMsgEnd = pos + ndefMsgLen;
    if (ndefMsgEnd > totalRead) ndefMsgEnd = totalRead;

    String textResult = "";

    // Walk through NDEF records inside the message
    while (pos < ndefMsgEnd) {
        byte hdr = pages[pos++];
        byte tnf  = hdr & 0x07;
        bool me   = (hdr >> 6) & 1;
        bool sr   = (hdr >> 4) & 1;
        bool il   = (hdr >> 3) & 1;

        if (pos >= ndefMsgEnd) break;
        byte typeLen = pages[pos++];

        int payloadLen;
        if (sr) {
            if (pos >= ndefMsgEnd) break;
            payloadLen = pages[pos++];
        } else {
            if (pos + 3 >= ndefMsgEnd) break;
            payloadLen = ((uint32_t)pages[pos] << 24) | ((uint32_t)pages[pos+1] << 16)
                       | ((uint32_t)pages[pos+2] << 8) | pages[pos+3];
            pos += 4;
        }

        if (il) {
            if (pos >= ndefMsgEnd) break;
            pos += pages[pos] + 1;  // skip ID-length byte + ID
        }

        byte recordType = 0;
        if (typeLen >= 1 && pos < ndefMsgEnd) recordType = pages[pos];
        pos += typeLen;

        int payloadStart = pos;
        pos += payloadLen;

        if (tnf == 0x01 && typeLen == 1 && recordType == 0x55) {
            ndefHasUri = true;
            Serial.println("Found NDEF URI record");
        } else if (tnf == 0x01 && typeLen == 1 && recordType == 0x54 && textResult.length() == 0) {
            if (payloadStart < ndefMsgEnd) {
                byte st      = pages[payloadStart];
                byte langLen = st & 0x3F;
                int  txtOff  = payloadStart + 1 + langLen;
                int  txtLen  = payloadLen - 1 - langLen;
                if (txtLen > 0 && txtOff + txtLen <= totalRead) {
                    char text[64] = {0};
                    int cl = min(txtLen, 63);
                    memcpy(text, pages + txtOff, cl);
                    text[cl] = '\0';
                    textResult = String(text);
                    Serial.print("Found NDEF Text: ");
                    Serial.println(textResult);
                }
            }
        }

        if (me) break;
    }

    return textResult;
}

// ──────────────────────────────────────────────
// NDEF URI writer for NTAG215
//
// Writes a URI NDEF record to the tag using MIFARE_Ultralight_Write
// (4 bytes per page). If the tag already contained a Text record
// (e.g. "GOLDEN:Free T-Shirt"), that record is preserved as a second
// NDEF record so both the phone URL and the M5Stick prize still work.
//
// Guarded by #ifdef NFC_WRITE_URL — the feature compiles out entirely
// when the define is commented out in config.h.
// ──────────────────────────────────────────────

#ifdef NFC_WRITE_URL
bool writeNDEFUri(const String &preserveText) {
    const char *url = NFC_WRITE_URL;

    // Match longest URI prefix first to pick the right NDEF abbreviation code
    byte prefixCode    = 0x00;
    const char *urlBody = url;

    if      (strncmp(url, "https://www.", 12) == 0) { prefixCode = 0x02; urlBody += 12; }
    else if (strncmp(url, "http://www.",  11) == 0) { prefixCode = 0x01; urlBody += 11; }
    else if (strncmp(url, "https://",     8)  == 0) { prefixCode = 0x04; urlBody += 8;  }
    else if (strncmp(url, "http://",      7)  == 0) { prefixCode = 0x03; urlBody += 7;  }

    int urlBodyLen    = strlen(urlBody);
    int uriPayloadLen = 1 + urlBodyLen;   // prefix byte + URL body

    bool hasText       = preserveText.length() > 0;
    int  textPayloadLen = 0;
    if (hasText) textPayloadLen = 1 + 2 + preserveText.length();  // status + "en" + text

    // --- Build NDEF records into msg[] ---
    byte msg[160];
    int  p = 0;

    // URI record  (MB=1, SR=1, TNF=001; ME depends on whether a Text record follows)
    msg[p++] = hasText ? 0x91 : 0xD1;
    msg[p++] = 1;                        // type length
    msg[p++] = (byte)uriPayloadLen;      // payload length (short record)
    msg[p++] = 0x55;                     // type "U"
    msg[p++] = prefixCode;
    memcpy(msg + p, urlBody, urlBodyLen);
    p += urlBodyLen;

    // Optional Text record to preserve golden-sticker data
    if (hasText) {
        msg[p++] = 0x51;                // MB=0, ME=1, SR=1, TNF=001
        msg[p++] = 1;                   // type length
        msg[p++] = (byte)textPayloadLen; // payload length
        msg[p++] = 0x54;                // type "T"
        msg[p++] = 0x02;                // status: UTF-8, lang len = 2
        msg[p++] = 'e';
        msg[p++] = 'n';
        memcpy(msg + p, preserveText.c_str(), preserveText.length());
        p += preserveText.length();
    }

    // --- Wrap in TLV: 03 <len> <msg…> FE ---
    byte tlv[168];
    int  t = 0;
    tlv[t++] = 0x03;
    tlv[t++] = (byte)p;   // message length (always < 255 for our URLs)
    memcpy(tlv + t, msg, p);
    t += p;
    tlv[t++] = 0xFE;      // terminator TLV

    while (t % 4 != 0) tlv[t++] = 0x00;  // pad to page boundary

    int numPages = t / 4;
    Serial.printf("Writing NDEF URI (%d bytes, %d pages)\n", t, numPages);

    for (int i = 0; i < numPages; i++) {
        byte page = 4 + i;
        if (mfrc522.MIFARE_Ultralight_Write(page, tlv + (i * 4), 4) != MFRC522_I2C::STATUS_OK) {
            Serial.printf("Write failed on page %d\n", page);
            return false;
        }
    }

    Serial.println("NDEF URI written OK");
    return true;
}
#endif

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

    // Read all NDEF records: finds Text for golden stickers, flags URI presence
    String ndefText = readNDEF();

#ifdef NFC_WRITE_URL
    if (!ndefHasUri) {
        writeNDEFUri(ndefText);
    }
#endif

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
