// Written for Grafana Labs – GrafanaCon Science Fair IoT booth.
// SECONDARY PIR device: detects motion and sends ESP-NOW trigger events
// to the PRIMARY device. No WiFi or Grafana needed on this device.
//
// Place this device on the INNER (booth) side of the entrance,
// ~30-50 cm from the PRIMARY device.
//
// Hardware: M5StickC Plus2 + Unit PIR on Grove Port A (signal on GPIO 33)
// Platform: Arduino M5Stack Board Manager
// Dependent Libraries:
//   M5StickCPlus2: https://github.com/m5stack/M5StickCPlus2

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <esp_now.h>

#include "config.h"

#define PIR_PIN 33

static const unsigned long TRIGGER_COOLDOWN_MS = 1000;

struct PIREvent {
    uint8_t sensor_id;  // 1 = secondary
};

static uint8_t peerMAC[] = PRIMARY_MAC;
static bool espNowReady = false;
static unsigned long lastTrigger = 0;
static int triggerCount = 0;
static bool displayDirty = true;

// ──────────────────────────────────────────────
// ESP-NOW
// ──────────────────────────────────────────────

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void initESPNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

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
    Serial.println("ESP-NOW sender ready");
}

// ──────────────────────────────────────────────
// LCD
// ──────────────────────────────────────────────

void drawUI() {
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(MAGENTA);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 5);
    StickCP2.Display.println("PIR Sensor B");

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 35);
    StickCP2.Display.println("Active");

    StickCP2.Display.setTextColor(WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 65);
    StickCP2.Display.printf("Triggers: %d", triggerCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 95);
    StickCP2.Display.print("MAC:");
    StickCP2.Display.println(WiFi.macAddress());

    StickCP2.Display.setCursor(10, 108);
    StickCP2.Display.println("(booth inside)");
}

void flashMotion() {
    StickCP2.Display.fillScreen(MAGENTA);
    StickCP2.Display.setTextColor(BLACK);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(20, 40);
    StickCP2.Display.println("MOTION!");
    delay(200);
    displayDirty = true;
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
    StickCP2.Display.setTextColor(MAGENTA);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("PIR Secondary");
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("Starting ESP-NOW...");

    pinMode(PIR_PIN, INPUT);

    initESPNow();

    drawUI();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();
    unsigned long now = millis();

    if (digitalRead(PIR_PIN) == HIGH && (now - lastTrigger > TRIGGER_COOLDOWN_MS)) {
        lastTrigger = now;
        triggerCount++;
        Serial.printf("PIR B triggered (#%d)\n", triggerCount);

        if (espNowReady) {
            PIREvent evt = { 1 };
            esp_now_send(peerMAC, (uint8_t *)&evt, sizeof(evt));
        }

        flashMotion();
    }

    if (displayDirty) {
        drawUI();
        displayDirty = false;
    }

    delay(50);
}
