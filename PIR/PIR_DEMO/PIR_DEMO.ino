// Written for Grafana Labs – GrafanaCon Science Fair IoT booth.
// PIR zone counter with dwell-time tracking. Detects "presence sessions":
// a session starts when motion is first detected and ends when the PIR
// has been LOW for SESSION_TIMEOUT_MS (person left). Each session sends
// a visit count and dwell time (seconds) to Grafana Cloud.
//
// A live heartbeat (occupied + session_dwell) is sent every 10s directly
// from the HTTP task using current state, so it's always fresh.
//
// Place inside the booth near the demo table to measure visitor engagement.
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

static const unsigned long SESSION_TIMEOUT_MS    = 10000;
static const unsigned long HEARTBEAT_INTERVAL_MS = 10000;

enum MetricType { METRIC_VISIT, METRIC_DWELL };

struct MetricPayload {
    MetricType type;
    int dwell_seconds;
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static int visitCount = 0;

// Session state (shared with HTTP task -- volatile for cross-task reads)
static volatile bool    inSession     = false;
static volatile unsigned long sessionStart = 0;
static volatile unsigned long lastMotionAt = 0;

static bool lastPIRState = false;
static bool displayDirty = true;
static unsigned long lastDisplayUpdate = 0;

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
//
// Processes queued events (visit, dwell) AND sends a fresh
// heartbeat every 5s by reading current session state directly.
// ──────────────────────────────────────────────

void sendHttpPost(void *parameter) {
    MetricPayload msg;

    String url = "https://" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write";
    Serial.print("Grafana URL: ");
    Serial.println(url);

    String zone = String(ZONE);
    zone.replace(" ", "\\ ");
    String location = String(LOCATION);
    location.replace(" ", "\\ ");

    unsigned long lastHB = 0;
    bool lastSentOccupied = false;

    int consecutiveFails = 0;

    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
    client->setTimeout(5);

    auto resetConnection = [&]() {
        Serial.println("Full connection reset...");
        client->stop();
        WiFi.disconnect(true);
        delay(1000);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
            delay(500);
            retries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi reconnected");
            consecutiveFails = 0;
        } else {
            Serial.println("WiFi reconnect failed");
        }
    };

    auto doPost = [&](const String &postData) -> int {
        if (WiFi.status() != WL_CONNECTED) {
            resetConnection();
            if (WiFi.status() != WL_CONNECTED) return -1;
        }
        HTTPClient http;
        http.setTimeout(5000);
        http.begin(*client, url);
        http.addHeader("Content-Type", "text/plain");
        http.setAuthorization(GC_USER, GC_PASS);
        int rc = http.POST(postData);
        http.end();
        client->stop();
        if (rc > 0) {
            consecutiveFails = 0;
        } else {
            consecutiveFails++;
            if (consecutiveFails >= 3) {
                Serial.println("Resetting connection after 3 failures");
                resetConnection();
            }
        }
        return rc;
    };

    while (true) {
        if (xQueueReceive(httpQueue, &msg, pdMS_TO_TICKS(1000))) {
            String postData;
            if (msg.type == METRIC_VISIT) {
                postData = "m5PIR,location=" + location
                    + ",zone=" + zone + " count=1";
            } else {
                postData = "m5PIR,location=" + location
                    + ",zone=" + zone
                    + " dwell=" + String(msg.dwell_seconds);
            }

            Serial.print("POST: ");
            Serial.println(postData);
            int rc = doPost(postData);
            Serial.print("Grafana POST rc=");
            Serial.println(rc);
        }

        unsigned long now = millis();
        bool skipHB = consecutiveFails >= 3;
        if (!skipHB && (now - lastHB >= HEARTBEAT_INTERVAL_MS)) {
            lastHB = now;

            bool occupied = inSession;
            int dwellNow = 0;
            if (occupied) {
                dwellNow = (now - sessionStart) / 1000;
            }

            if (occupied || lastSentOccupied) {
                String postData = "m5PIR,location=" + location
                    + ",zone=" + zone
                    + " occupied=" + String(occupied ? 1 : 0)
                    + ",session_dwell=" + String(dwellNow);

                Serial.print("HB: ");
                Serial.println(postData);
                int rc = doPost(postData);
                Serial.print("HB rc=");
                Serial.println(rc);

                lastSentOccupied = occupied;
            }
        } else if (skipHB && (now - lastHB >= HEARTBEAT_INTERVAL_MS)) {
            lastHB = now;
            Serial.println("HB skipped (connection unhealthy)");
        }
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
    StickCP2.Display.printf("PIR: %s", ZONE);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 30);
    StickCP2.Display.printf("%d", visitCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(100, 35);
    StickCP2.Display.println("visits");

    if (inSession) {
        bool pirActive = digitalRead(PIR_PIN) == HIGH;
        int elapsed = (lastMotionAt - sessionStart) / 1000;
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, 75);
        if (pirActive) {
            elapsed = (millis() - sessionStart) / 1000;
            StickCP2.Display.setTextColor(YELLOW);
            StickCP2.Display.printf("Present %ds", elapsed);
        } else {
            StickCP2.Display.setTextColor(ORANGE);
            StickCP2.Display.printf("Left? %ds", elapsed);
        }
    } else {
        StickCP2.Display.setTextColor(TFT_DARKGREY);
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, 75);
        StickCP2.Display.println("Waiting...");
    }

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 108);
    StickCP2.Display.print("MAC:");
    StickCP2.Display.println(WiFi.macAddress());
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
    StickCP2.Display.printf("PIR: %s", ZONE);
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
    xTaskCreate(sendHttpPost, "sendHttpPost", 10240, NULL, 1, &httpTaskHandle);

    drawUI();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();
    unsigned long now = millis();

    bool pirNow = digitalRead(PIR_PIN) == HIGH;

    if (pirNow) {
        lastMotionAt = now;

        if (!inSession) {
            inSession = true;
            sessionStart = now;
            visitCount++;
            Serial.printf("Session #%d started\n", visitCount);

            MetricPayload m = { METRIC_VISIT, 0 };
            xQueueSend(httpQueue, &m, 0);
        }
    }

    if (inSession && !pirNow && (now - lastMotionAt >= SESSION_TIMEOUT_MS)) {
        int dwell = (lastMotionAt - sessionStart) / 1000;
        if (dwell < 1) dwell = 1;
        inSession = false;
        Serial.printf("Session ended, dwell: %ds\n", dwell);

        MetricPayload m = { METRIC_DWELL, dwell };
        xQueueSend(httpQueue, &m, 0);

        displayDirty = true;
    }

    if (inSession && (now - lastDisplayUpdate >= 1000)) {
        lastDisplayUpdate = now;
        displayDirty = true;
    }

    if (displayDirty) {
        drawUI();
        displayDirty = false;
    }

    delay(50);
}
