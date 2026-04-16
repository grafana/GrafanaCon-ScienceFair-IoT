// Written for Grafana Labs – GrafanaCon Science Fair IoT booth.
// Standalone PIR zone counter: detects motion events and pushes a count
// metric to Grafana Cloud with a configurable zone label.
//
// Deploy two of these at the booth:
//   PIR_AISLE  → faces the conference aisle, counts foot traffic
//   PIR_DEMO   → inside the booth near the demo, counts engaged visitors
//
// Conversion rate in Grafana:
//   m5PIR_count{zone="demo"} / m5PIR_count{zone="aisle"}
//
// Hardware: M5StickC Plus2 + Unit PIR on Grove Port A (signal on GPIO 33)
// Platform: Arduino M5Stack Board Manager
// Dependent Libraries:
//   M5StickCPlus2: https://github.com/m5stack/M5StickCPlus2

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "config.h"

#define PIR_PIN 33

// Minimum gap between counted events (PIR hold time is ~2s)
static const unsigned long EVENT_COOLDOWN_MS = 4000;

struct MetricPayload {
    int count;
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static int motionCount = 0;
static unsigned long lastEventTime = 0;
static bool lastPIRState = false;
static bool displayDirty = true;

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
                String zone = String(ZONE);
                zone.replace(" ", "\\ ");
                String location = String(LOCATION);
                location.replace(" ", "\\ ");

                String postData = "m5PIR,location=" + location
                    + ",zone=" + zone
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
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 5);
    StickCP2.Display.printf("Motion: %s", ZONE);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(4);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.printf("%d", motionCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 85);
    StickCP2.Display.println("detections");
}

void flashDetection() {
    StickCP2.Display.fillScreen(GREEN);
    StickCP2.Display.setTextColor(BLACK);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 15);
    StickCP2.Display.printf("#%d", motionCount);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 55);
    StickCP2.Display.println("MOTION");
    delay(300);
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
    StickCP2.Display.setTextColor(CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.printf("Motion: %s", ZONE);
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
    xTaskCreate(sendHttpPost, "sendHttpPost", 8192, NULL, 1, &httpTaskHandle);

    drawUI();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();
    unsigned long now = millis();

    bool pirNow = digitalRead(PIR_PIN) == HIGH;

    // Rising-edge detection with cooldown
    if (pirNow && !lastPIRState && (now - lastEventTime >= EVENT_COOLDOWN_MS)) {
        lastEventTime = now;
        motionCount++;
        Serial.printf("Motion #%d in zone %s\n", motionCount, ZONE);

        MetricPayload m = { motionCount };
        if (xQueueSend(httpQueue, &m, 0) != pdPASS) {
            Serial.println("HTTP queue full, dropping metric");
        }

        flashDetection();
    }
    lastPIRState = pirNow;

    if (displayDirty) {
        drawUI();
        displayDirty = false;
    }

    delay(50);
}
