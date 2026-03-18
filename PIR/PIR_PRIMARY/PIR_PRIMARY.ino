// Written for Grafana Labs – GrafanaCon Science Fair IoT booth.
// PRIMARY PIR device: connects to WiFi, receives ESP-NOW triggers from the
// SECONDARY PIR device, correlates trigger order to determine direction
// (enter / exit / pass-by), and pushes visitor metrics to Grafana Cloud.
//
// Place this device on the OUTER (corridor) side of the booth entrance.
// Place the SECONDARY device ~30-50 cm away on the INNER (booth) side.
//
// Direction logic:
//   A (this) triggers first, then B within DIRECTION_WINDOW_MS  →  ENTER
//   B triggers first, then A within DIRECTION_WINDOW_MS          →  EXIT
//   Only one triggers (no pair within window)                    →  PASS-BY
//
// Hardware: M5StickC Plus2 + Unit PIR on Grove Port A (signal on GPIO 33)
// Platform: Arduino M5Stack Board Manager
// Dependent Libraries:
//   M5StickCPlus2: https://github.com/m5stack/M5StickCPlus2

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>

#include "config.h"

#define PIR_PIN 33

// Direction correlation timing
static const unsigned long DIRECTION_WINDOW_MS = 2000;
static const unsigned long EVENT_COOLDOWN_MS   = 3000;

struct PIREvent {
    uint8_t sensor_id;  // 1 = secondary
};

struct MetricPayload {
    char direction[8];  // "enter", "exit", "passby"
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static int enterCount  = 0;
static int exitCount   = 0;
static int passbyCount = 0;

// Trigger timestamps (0 = not triggered)
static volatile unsigned long triggerA = 0;
static volatile unsigned long triggerB = 0;
static unsigned long lastEventTime = 0;

static bool displayDirty = true;

// ──────────────────────────────────────────────
// ESP-NOW receive callback (from secondary PIR)
// ──────────────────────────────────────────────

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(PIREvent)) return;
    PIREvent evt;
    memcpy(&evt, data, sizeof(evt));
    if (evt.sensor_id == 1) {
        triggerB = millis();
        Serial.println("ESP-NOW: sensor B triggered");
    }
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
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
}

// ──────────────────────────────────────────────
// ESP-NOW init
// ──────────────────────────────────────────────

void initESPNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
    Serial.println("ESP-NOW receiver ready");
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
                String location = String(LOCATION);
                location.replace(" ", "\\ ");

                String postData = "m5PIR,location=" + location
                    + ",direction=" + String(msg.direction)
                    + " count=1";

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
// LCD
// ──────────────────────────────────────────────

void drawUI() {
    StickCP2.Display.fillScreen(BLACK);

    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 5);
    StickCP2.Display.println("Booth Traffic");

    StickCP2.Display.setTextSize(2);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setCursor(10, 35);
    StickCP2.Display.printf("Enter: %d", enterCount);

    StickCP2.Display.setTextColor(ORANGE);
    StickCP2.Display.setCursor(10, 58);
    StickCP2.Display.printf("Exit:  %d", exitCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setCursor(10, 81);
    StickCP2.Display.printf("Pass:  %d", passbyCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 108);
    StickCP2.Display.print("MAC:");
    StickCP2.Display.println(WiFi.macAddress());
}

void flashDirection(const char *label, uint16_t color) {
    StickCP2.Display.fillScreen(color);
    StickCP2.Display.setTextColor(BLACK);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(15, 40);
    StickCP2.Display.println(label);
    delay(400);
    displayDirty = true;
}

// ──────────────────────────────────────────────
// Direction logic
// ──────────────────────────────────────────────

void resolveDirection() {
    unsigned long now = millis();

    if (now - lastEventTime < EVENT_COOLDOWN_MS) return;

    bool aTriggered = (triggerA > 0);
    bool bTriggered = (triggerB > 0);

    if (!aTriggered && !bTriggered) return;

    // Both triggered within the direction window → determine order
    if (aTriggered && bTriggered) {
        unsigned long diff = (triggerA > triggerB)
            ? (triggerA - triggerB) : (triggerB - triggerA);

        if (diff <= DIRECTION_WINDOW_MS) {
            MetricPayload payload = {};
            if (triggerA < triggerB) {
                enterCount++;
                strncpy(payload.direction, "enter", sizeof(payload.direction));
                Serial.printf(">>> ENTER (#%d)\n", enterCount);
                flashDirection("ENTER", GREEN);
            } else {
                exitCount++;
                strncpy(payload.direction, "exit", sizeof(payload.direction));
                Serial.printf("<<< EXIT (#%d)\n", exitCount);
                flashDirection("EXIT", ORANGE);
            }
            xQueueSend(httpQueue, &payload, 0);
            triggerA = 0;
            triggerB = 0;
            lastEventTime = now;
            return;
        }
    }

    // Single sensor triggered and window has expired → pass-by
    if (aTriggered && !bTriggered && (now - triggerA > DIRECTION_WINDOW_MS)) {
        passbyCount++;
        MetricPayload payload = {};
        strncpy(payload.direction, "passby", sizeof(payload.direction));
        Serial.printf("--- PASS-BY (#%d)\n", passbyCount);
        xQueueSend(httpQueue, &payload, 0);
        triggerA = 0;
        lastEventTime = now;
        displayDirty = true;
        return;
    }

    if (bTriggered && !aTriggered && (now - triggerB > DIRECTION_WINDOW_MS)) {
        passbyCount++;
        MetricPayload payload = {};
        strncpy(payload.direction, "passby", sizeof(payload.direction));
        Serial.printf("--- PASS-BY (#%d)\n", passbyCount);
        xQueueSend(httpQueue, &payload, 0);
        triggerB = 0;
        lastEventTime = now;
        displayDirty = true;
        return;
    }
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
    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.println("PIR Primary");
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("Connecting WiFi...");

    pinMode(PIR_PIN, INPUT);

    MetricPayload d = {};
    httpQueue = xQueueCreate(10, sizeof(d));
    if (httpQueue == NULL) {
        Serial.println("Error creating queue!");
        return;
    }

    initWifi();
    initESPNow();

    xTaskCreate(sendHttpPost, "sendHttpPost", 8192, NULL, 1, &httpTaskHandle);

    drawUI();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();

    // Read local PIR (sensor A)
    if (digitalRead(PIR_PIN) == HIGH && triggerA == 0) {
        triggerA = millis();
        Serial.println("PIR A triggered (local)");
    }

    resolveDirection();

    if (displayDirty) {
        drawUI();
        displayDirty = false;
    }

    delay(50);
}
