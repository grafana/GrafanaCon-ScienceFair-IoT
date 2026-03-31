// Environmental sensor station — Temp, Humidity, Pressure, VOC, CO2, Lux → Grafana Cloud
//
// Originally by Willie Engelbrecht (GrafanaCon 2025), rewritten for InfluxDB line protocol.
//
// Hardware: M5StickC Plus2 + ENV III Hat + SGP30 (VOC/CO2) + DLight (Lux)
// Dependent Libraries:
//   M5Unified       — https://github.com/m5stack/M5Unified
//   M5UnitENV       — https://github.com/m5stack/M5Unit-ENV
//   Adafruit_SGP30  — https://github.com/adafruit/Adafruit_SGP30
//   M5_DLight       — M5Stack DLight unit library

#include "M5Unified.h"
#include "M5UnitENV.h"
#include "Adafruit_SGP30.h"
#include <M5_DLight.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"

#define HTTP_TIMEOUT_MS     5000
#define RESTART_AFTER_FAILS 10
#define SEND_INTERVAL_MS    5000

SHT3X sht30;
QMP6988 qmp6988;
Adafruit_SGP30 sgp;
M5_DLight dlight;

float temp     = 0.0;
float hum      = 0.0;
float pressure = 0.0;
int   voc      = 0;
int   co2      = 0;
uint16_t lux   = 0;

int sendFailCount    = 0;
int lastHttpCode     = 0;
unsigned long lastSendTime = 0;

WiFiClientSecure secureClient;
HTTPClient http;
bool httpConnected = false;

void setupHttp() {
    secureClient.setInsecure();
    secureClient.setTimeout(HTTP_TIMEOUT_MS / 1000);
    String url = "https://" + String(GC_USER) + ":" + String(GC_PASS) +
                 "@" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write";
    http.begin(secureClient, url);
    http.addHeader("Content-Type", "text/plain");
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(true);
    httpConnected = true;
    Serial.println("HTTP connection initialized");
}

void initWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++attempts > 30) {
            Serial.println("\nWiFi failed");
            return;
        }
    }
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    Serial.println("Booting up!");

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(3);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.printf("== Grafana Labs ==");
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(10, 35);
    M5.Display.printf("Environmental");
    M5.Display.setCursor(10, 60);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.printf("Loc: %s", LOCATION);
    M5.Display.setCursor(10, 85);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.printf("Connecting...");

    Wire.begin();
    if (!qmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 32, 33, 400000U))
        Serial.println("QMP6988 not found");
    else
        Serial.println("Found Pressure sensor");

    if (!sht30.begin(&Wire, SHT3X_I2C_ADDR, 32, 33, 400000U))
        Serial.println("SHT3X not found");
    else
        Serial.println("Found Temp/Humidity sensor");

    if (!sgp.begin())
        Serial.println("SGP30 not found");
    else
        Serial.println("Found VOC/CO2 sensor");

    dlight.begin();
    dlight.setMode(CONTINUOUSLY_H_RESOLUTION_MODE2);
    Serial.println("Found Lux sensor");

    initWifi();

    M5.Display.setCursor(10, 110);
    if (WiFi.status() == WL_CONNECTED) {
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.printf("WiFi: OK");
        setupHttp();
    } else {
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.printf("WiFi: RETRY");
    }
    delay(1000);

    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 15000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);

    Serial.println("End of setup()");
}

void loop() {
    esp_task_wdt_reset();
    unsigned long loopStart = millis();
    Serial.printf("\r\n====================================\r\n");

    if (qmp6988.update()) {
        pressure = qmp6988.calcPressure();
    }
    if (sht30.update()) {
        temp = sht30.cTemp;
        hum  = sht30.humidity;
    } else {
        temp = 0;
        hum = 0;
    }
    Serial.printf("Temp: %2.1f C  Hum: %2.0f%%  Pres: %2.0f hPa\n", temp, hum, pressure / 100);

    if (!sgp.IAQmeasure()) {
        Serial.println("VOC/CO2 measurement failed");
    }
    voc = int(sgp.TVOC);
    co2 = int(sgp.eCO2);
    Serial.printf("eCO2: %d ppm  TVOC: %d ppb\n", co2, voc);

    lux = dlight.getLUX();
    Serial.printf("Lux: %d\n", lux);

    int bat_volt  = M5.Power.getBatteryVoltage();
    int bat_level = M5.Power.getBatteryLevel();
    Serial.printf("Bat: %dmV  %d%%\n", bat_volt, bat_level);

    // Display (always update — keeps screen real-time)
    M5.Lcd.fillRect(0, 0, 256, 256, BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.printf("== Grafana Labs ==");
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(0, 40);
    M5.Display.printf(" Temp: %2.1f  \r\n Humi: %2.0f%%  \r\n Pres:%2.0f hPa\r\n", temp, hum, pressure / 100);
    M5.Display.printf(" VOC:%d CO2:%d\r\n LUX:%d\r\n", voc, co2, lux);

    // Send to Grafana at intervals (not every loop)
    unsigned long sendMs = 0;
    if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
        lastSendTime = millis();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost — reconnecting");
            httpConnected = false;
            WiFi.disconnect();
            initWifi();
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi still down — skipping send");
                sendFailCount++;
                return;
            }
        }

        if (!httpConnected) {
            setupHttp();
        }

        unsigned long sendStart = millis();

        String postData = String(MEASUREMENT_NAME) + ",location=" + String(LOCATION) +
            " temperature=" + String(temp, 2) +
            ",humidity="    + String(hum, 2) +
            ",pressure="    + String(pressure, 2) +
            ",co2="         + String(co2) +
            ",voc="         + String(voc) +
            ",lux="         + String(lux) +
            ",bat_volt="    + String(bat_volt) +
            ",bat_level="   + String(bat_level);

        lastHttpCode = http.POST(postData);
        sendMs = millis() - sendStart;

        if (lastHttpCode == 204) {
            sendFailCount = 0;
            Serial.printf("HTTP: 204 OK (%lums)\n", sendMs);
        } else {
            sendFailCount++;
            Serial.printf("HTTP: %d FAIL (%lums) [%d consecutive]\n", lastHttpCode, sendMs, sendFailCount);
            http.end();
            httpConnected = false;
            if (sendFailCount >= RESTART_AFTER_FAILS) {
                Serial.println("Too many failures — restarting");
                ESP.restart();
            }
        }
    }

    unsigned long loopMs = millis() - loopStart;
    if (sendMs > 0) Serial.printf("Loop: %lums (send: %lums)\n", loopMs, sendMs);

    // Button A: show system status
    unsigned long btnStart = millis();
    while (millis() - btnStart < 1000) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            M5.Lcd.fillRect(0, 0, 256, 256, BLACK);
            M5.Display.setCursor(10, 10);
            M5.Display.setTextColor(ORANGE, BLACK);
            M5.Display.printf("== Status ==");
            M5.Display.setTextColor(WHITE, BLACK);

            M5.Display.setCursor(0, 40);
            M5.Display.printf(" WiFi: ");
            if (WiFi.status() == WL_CONNECTED) {
                M5.Display.setTextColor(GREEN, BLACK);
                M5.Display.printf("OK (%ddBm)", WiFi.RSSI());
            } else {
                M5.Display.setTextColor(RED, BLACK);
                M5.Display.printf("Disconnected");
            }

            M5.Display.setTextColor(WHITE, BLACK);
            M5.Display.setCursor(0, 65);
            M5.Display.printf(" HTTP: ");
            if (lastHttpCode == 204) {
                M5.Display.setTextColor(GREEN, BLACK);
                M5.Display.printf("OK");
            } else {
                M5.Display.setTextColor(RED, BLACK);
                M5.Display.printf("%d (fail:%d)", lastHttpCode, sendFailCount);
            }

            M5.Display.setTextColor(WHITE, BLACK);
            M5.Display.setCursor(0, 90);
            M5.Display.printf(" Bat: %dmV %d%%", bat_volt, bat_level);
            M5.Display.setCursor(0, 115);
            M5.Display.printf(" Up: %lum", millis() / 60000);
        }
        delay(50);
    }
}
