// ToF4M Aisle Counter — beam-break foot traffic counter for GrafanaCon Science Fair.
//
// Uses the M5Stack Unit ToF4M (VL53L1X) as a laser tripwire across the aisle.
// When someone walks through the beam, the measured distance drops below
// BEAM_THRESHOLD_MM → one event counted → metric pushed to Grafana Cloud.
//
// Sends the same m5PIR metric as PIR_AISLE, so it's a drop-in replacement
// on existing dashboards.  count=1 per event, sum() in Grafana for totals.
//
// Hardware: M5StickC Plus2 + Unit ToF4M on Grove Port A (I2C)
// Dependent Libraries:
//   M5StickCPlus2  — https://github.com/m5stack/M5StickCPlus2
//   M5Unit-ToF4M   — https://github.com/m5stack/M5Unit-ToF4M  (brings in VL53L1X)

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <VL53L1X.h>

#include "config.h"

VL53L1X tof;

struct MetricPayload {
    int count;
    int distance;
    int blocked;
};

TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

static int     eventCount      = 0;
static int     lastSentCount   = 0;
static bool    beamBlocked     = false;
static unsigned long lastEventTime  = 0;
static unsigned long lastSendTime   = 0;
static unsigned long lastDrawTime   = 0;
static int     lastDistanceMM  = 0;
static bool    sensorOK        = false;

// ──────────────────────────────────────────────
// WiFi
// ──────────────────────────────────────────────

void initWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++attempts > 40) ESP.restart();
    }
    Serial.println();
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
}

// ──────────────────────────────────────────────
// Async HTTP task (FreeRTOS) — runs on core 0
// ──────────────────────────────────────────────

WiFiClientSecure *newSecureClient() {
    WiFiClientSecure *c = new WiFiClientSecure;
    c->setInsecure();
    c->setTimeout(20);
    return c;
}

void sendHttpPost(void *parameter) {
    MetricPayload msg;
    String url = "https://" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write";

    Serial.print("Grafana URL: ");
    Serial.println(url);

    WiFiClientSecure *client = newSecureClient();

    while (true) {
        if (xQueueReceive(httpQueue, &msg, portMAX_DELAY)) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi disconnected, skipping send");
                continue;
            }

            String zone = String(ZONE);
            zone.replace(" ", "\\ ");
            String location = String(LOCATION);
            location.replace(" ", "\\ ");

            String postData = "m5PIR,location=" + location
                + ",zone=" + zone
                + " count=" + String(msg.count)
                + "\nm5ToF,location=" + location
                + ",zone=" + zone
                + " distance=" + String(msg.distance)
                + ",blocked=" + String(msg.blocked);

            HTTPClient http;
            http.begin(*client, url);
            http.addHeader("Content-Type", "text/plain");
            http.setAuthorization(GC_USER, GC_PASS);
            http.setTimeout(20000);

            int rc = http.POST(postData);
            Serial.print("Grafana POST rc=");
            Serial.println(rc);

            http.end();

            if (rc < 0) {
                delete client;
                client = newSecureClient();
            }
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
    StickCP2.Display.printf("ToF: %s", ZONE);

    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 35);
    StickCP2.Display.printf("%d", eventCount);

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 70);
    StickCP2.Display.println("detections");

    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 95);
    StickCP2.Display.printf("Dist:%dmm <%d >%d", lastDistanceMM, BEAM_ENTER_MM, BEAM_EXIT_MM);

    StickCP2.Display.setCursor(10, 110);
    StickCP2.Display.printf("Beam: %s", beamBlocked ? "BLOCKED" : "clear");
}

void flashDetection() {
    StickCP2.Display.fillScreen(GREEN);
    StickCP2.Display.setTextColor(BLACK);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(10, 15);
    StickCP2.Display.printf("#%d", eventCount);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 55);
    StickCP2.Display.println("BEAM BREAK");
    delay(200);
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
    StickCP2.Display.println("ToF4M Aisle");
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("Init sensor...");

    Wire.begin(32, 33);
    tof.setBus(&Wire);
    tof.setTimeout(500);

    if (!tof.init()) {
        Serial.println("ToF4M sensor init failed!");
        StickCP2.Display.setCursor(10, 55);
        StickCP2.Display.setTextColor(RED);
        StickCP2.Display.println("SENSOR FAILED");
        while (true) delay(1000);
    }

    sensorOK = true;
    tof.setDistanceMode(VL53L1X::Long);
    tof.setMeasurementTimingBudget(200000);
    tof.startContinuous(220);

    Serial.println("ToF4M ready");
    Serial.printf("Beam enter: %dmm  exit: %dmm\n", BEAM_ENTER_MM, BEAM_EXIT_MM);

    StickCP2.Display.setCursor(10, 55);
    StickCP2.Display.println("Connecting WiFi...");

    MetricPayload d = {};
    httpQueue = xQueueCreate(1, sizeof(d));
    if (httpQueue == NULL) {
        Serial.println("Error creating queue!");
        return;
    }

    initWifi();
    xTaskCreatePinnedToCore(sendHttpPost, "sendHttpPost", 16384, NULL, 1, &httpTaskHandle, 0);

    drawUI();
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();

    if (!sensorOK) return;

    int dist = tof.read(false);
    if (tof.timeoutOccurred()) return;

    lastDistanceMM = dist;
    unsigned long now = millis();

    // Hysteresis: enter at BEAM_ENTER_MM, exit at BEAM_EXIT_MM
    if (!beamBlocked && dist > 0 && dist < BEAM_ENTER_MM) {
        beamBlocked = true;
        if (now - lastEventTime >= EVENT_COOLDOWN_MS) {
            lastEventTime = now;
            eventCount++;
            Serial.printf("Beam break #%d — dist=%dmm zone=%s\n", eventCount, dist, ZONE);
            flashDetection();
        }
    } else if (beamBlocked && dist > BEAM_EXIT_MM) {
        beamBlocked = false;
    }

    // Send count + distance to Grafana every 5s
    if (now - lastSendTime >= 5000) {
        lastSendTime = now;
        MetricPayload m = { eventCount, lastDistanceMM, beamBlocked ? 1 : 0 };
        xQueueOverwrite(httpQueue, &m);
        Serial.printf("Queued count=%d dist=%dmm blocked=%d for Grafana\n", eventCount, lastDistanceMM, m.blocked);
    }

    // Refresh display every 500ms
    if (now - lastDrawTime >= 500) {
        lastDrawTime = now;
        drawUI();
    }

    delay(50);
}
