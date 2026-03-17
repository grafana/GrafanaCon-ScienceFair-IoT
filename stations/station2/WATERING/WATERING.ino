// Written for Grafana Labs to demonstrate how to use the M5StickC Plus2 with Grafana Cloud
// Watering Unit receives ESP-NOW pump commands from the RFID reader, controls the
// water pump for a timed duration, reads the moisture sensor, and pushes all metrics
// to Grafana Cloud via the Influx HTTP API.
//
// Hardware: M5StickC Plus2 + Unit Watering (pump + moisture) on Grove Port A
//   G33 = moisture sensor ADC input
//   G32 = pump digital output
// Platform: Arduino M5Stack Board Manager
// Dependent Libraries:
//   M5StickCPlus2: https://github.com/m5stack/M5StickCPlus2

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>

#include "config.h"

// ESP-NOW command struct (must match the RFID sender)
struct WaterCommand {
    uint8_t command;       // 1 = start pump
    uint8_t duration_sec;  // how many seconds to run
};

// Data queued for the Grafana HTTP sender task
enum MetricType { METRIC_MOISTURE, METRIC_PUMP_EVENT };

struct MetricPayload {
    MetricType type;
    int        moisture;       // soil moisture percentage
    int        pump_active;    // 1 = on, 0 = off
    int        duration;       // pump run duration in seconds
};

// FreeRTOS handles for async HTTP sending
TaskHandle_t  httpTaskHandle = NULL;
QueueHandle_t httpQueue;

// Pump state (non-blocking timer)
static volatile bool     pumpRunning     = false;
static volatile uint8_t  pumpDuration    = 0;
static unsigned long     pumpStartTime   = 0;
static unsigned long     pumpEndTime     = 0;

// Moisture reading interval
static unsigned long lastMoistureSend = 0;

// Display refresh control -- only redraw when state changes
static unsigned long lastDisplayUpdate = 0;
static int           lastMoisturePct   = -1;
static bool          lastPumpState     = false;
static int           lastCountdown     = -1;

// ──────────────────────────────────────────────
// ESP-NOW receive callback
// Signature for M5Stack board manager >= 3.0 (ESP-IDF 5.x / Arduino ESP32 core 3.x).
// If compiling with board manager 2.x, change to:
//   void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len)
// ──────────────────────────────────────────────

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(WaterCommand)) return;

    WaterCommand cmd;
    memcpy(&cmd, data, sizeof(cmd));

    Serial.printf("ESP-NOW rx: cmd=%d dur=%ds\n", cmd.command, cmd.duration_sec);

    if (cmd.command == 1 && cmd.duration_sec > 0) {
        pumpDuration  = cmd.duration_sec;
        pumpStartTime = millis();
        pumpEndTime   = pumpStartTime + (unsigned long)cmd.duration_sec * 1000UL;
        pumpRunning   = true;

        digitalWrite(PUMP_PIN, HIGH);
        Serial.printf("Pump ON for %ds\n", cmd.duration_sec);

        // Queue pump-ON event for Grafana
        MetricPayload m = { METRIC_PUMP_EVENT, 0, 1, cmd.duration_sec };
        xQueueSend(httpQueue, &m, 0);
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
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi channel: ");
    Serial.println(WiFi.channel());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
}

// ──────────────────────────────────────────────
// ESP-NOW init (receiver side)
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
                String postData;
                if (msg.type == METRIC_MOISTURE) {
                    postData = "m5Watering,location=home,plant=" + String(PLANT_NAME) + " moisture=" + String(msg.moisture);
                } else {
                    postData = "m5Watering,location=home,plant=" + String(PLANT_NAME) + " pump_active=" + String(msg.pump_active)
                             + ",duration=" + String(msg.duration);
                }

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
    StickCP2.Display.println("Watering Unit");
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 40);
    StickCP2.Display.println("Connecting WiFi...");

    pinMode(MOISTURE_PIN, INPUT);
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);

    MetricPayload d = {};
    httpQueue = xQueueCreate(10, sizeof(d));
    if (httpQueue == NULL) {
        Serial.println("Error creating queue!");
        return;
    }

    initWifi();
    initESPNow();

    xTaskCreate(
        sendHttpPost,
        "sendHttpPost",
        4096,
        NULL,
        1,
        &httpTaskHandle
    );

    StickCP2.Display.fillScreen(BLACK);
    drawUI(0, false, 0);
}

// ──────────────────────────────────────────────
// LCD drawing
// ──────────────────────────────────────────────

void drawUI(int moisturePct, bool pumping, int countdown) {
    StickCP2.Display.fillScreen(BLACK);

    // Title
    StickCP2.Display.setTextColor(GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 5);
    StickCP2.Display.println("Watering");

    // Moisture -- green when wet (high %), red when dry (low %)
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 35);
    if (moisturePct > 25) {
        StickCP2.Display.setTextColor(GREEN);
    } else {
        StickCP2.Display.setTextColor(RED);
    }
    StickCP2.Display.printf("Moisture:%d%%", moisturePct);

    // Pump status
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 65);
    if (pumping) {
        StickCP2.Display.setTextColor(CYAN);
        StickCP2.Display.printf("PUMP ON  %ds", countdown);
    } else {
        StickCP2.Display.setTextColor(TFT_DARKGREY);
        StickCP2.Display.println("PUMP OFF");
    }

    // MAC address for pairing reference
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(10, 95);
    StickCP2.Display.print("MAC:");
    StickCP2.Display.println(WiFi.macAddress());
}

// ──────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────

void loop() {
    StickCP2.update();
    unsigned long now = millis();

    // --- Non-blocking pump timer ---
    if (pumpRunning && now >= pumpEndTime) {
        pumpRunning = false;
        digitalWrite(PUMP_PIN, LOW);
        Serial.println("Pump OFF (timer expired)");

        MetricPayload m = { METRIC_PUMP_EVENT, 0, 0, pumpDuration };
        xQueueSend(httpQueue, &m, 0);
    }

    // Manual pump toggle with Button A (BtnA on StickC Plus2)
    if (StickCP2.BtnA.wasPressed()) {
        if (pumpRunning) {
            pumpRunning = false;
            digitalWrite(PUMP_PIN, LOW);
            Serial.println("Pump OFF (manual)");
            MetricPayload m = { METRIC_PUMP_EVENT, 0, 0, 0 };
            xQueueSend(httpQueue, &m, 0);
        } else {
            pumpDuration  = 5;
            pumpStartTime = now;
            pumpEndTime   = now + 5000UL;
            pumpRunning   = true;
            digitalWrite(PUMP_PIN, HIGH);
            Serial.println("Pump ON (manual 5s)");
            MetricPayload m = { METRIC_PUMP_EVENT, 0, 1, 5 };
            xQueueSend(httpQueue, &m, 0);
        }
    }

    // --- Read moisture sensor ---
    // Lower ADC = wetter soil, higher ADC = drier soil.
    // Invert so that moisture% rises with wetness.
    int rawADC       = analogRead(MOISTURE_PIN);
    int moisturePct  = map(rawADC, 4095, 1000, 0, 100);
    moisturePct      = constrain(moisturePct, 0, 100);

    // --- Send moisture to Grafana periodically ---
    if (now - lastMoistureSend >= MOISTURE_SEND_INTERVAL_MS) {
        lastMoistureSend = now;

        Serial.printf("Moisture: %d%% (raw=%d)\n", moisturePct, rawADC);

        MetricPayload m = { METRIC_MOISTURE, moisturePct, 0, 0 };
        if (xQueueSend(httpQueue, &m, 0) != pdPASS) {
            Serial.println("HTTP queue full, dropping moisture metric");
        }
    }

    // --- Update LCD only when state changes ---
    int countdown = 0;
    if (pumpRunning) {
        countdown = (int)((pumpEndTime - now) / 1000UL) + 1;
        if (countdown < 0) countdown = 0;
    }

    bool needsRedraw = (moisturePct != lastMoisturePct)
                    || (pumpRunning != lastPumpState)
                    || (countdown != lastCountdown);

    if (needsRedraw) {
        drawUI(moisturePct, pumpRunning, countdown);
        lastMoisturePct = moisturePct;
        lastPumpState   = pumpRunning;
        lastCountdown   = countdown;
    }

    delay(100);
}
