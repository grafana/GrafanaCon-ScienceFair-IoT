// ENV III Hat (SHT30 + QMP6988) — Temperature, Humidity, Pressure → Grafana Cloud
//
// Hardware: M5StickCPlus2 + ENV III Hat + 18650C Battery Hat
// Dependent Libraries:
//   M5Unified     — https://github.com/m5stack/M5Unified
//   M5UnitENV     — https://github.com/m5stack/M5Unit-ENV

#include <M5Unified.h>
#include "M5UnitENV.h"
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"

SHT3X sht3x;
QMP6988 qmp;

float temp     = 0.0;
float hum      = 0.0;
float pressure = 0.0;
float altitude = 0.0;

void initWifi() {
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
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(3);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.printf("== Grafana Labs ==");
    M5.Display.setCursor(10, 35);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.printf("ENV III Hat");
    M5.Display.setCursor(10, 60);
    M5.Display.printf("Connecting WiFi...");

    if (!qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 0, 26, 400000U)) {
        Serial.println("Couldn't find QMP6988 — is the hat connected?");
        M5.Display.setCursor(10, 85);
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.printf("NO SENSOR! Retry 5s");
        delay(5000);
        ESP.restart();
    }

    if (!sht3x.begin(&Wire, SHT3X_I2C_ADDR, 0, 26, 400000U)) {
        Serial.println("Couldn't find SHT3X — is the hat connected?");
        M5.Display.setCursor(10, 85);
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.printf("NO SENSOR! Retry 5s");
        delay(5000);
        ESP.restart();
    }

    initWifi();

    M5.Display.setCursor(10, 85);
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.printf("WiFi: OK");
    delay(2000);

    M5.Display.fillScreen(BLACK);
    M5.Lcd.setBrightness(0);
}

void loop() {
    M5.Lcd.setBrightness(0);

    if (sht3x.update()) {
        temp = sht3x.cTemp;
        hum  = sht3x.humidity;
    }

    if (qmp.update()) {
        pressure = qmp.calcPressure();
        altitude = qmp.altitude;
    }

    // Restart on bogus pressure — sensor sometimes locks up on long runs
    if (pressure < 950 || pressure / 100 > 1200) {
        Serial.println("Bad pressure reading — restarting");
        ESP.restart();
    }

    Serial.printf("Temp: %.1f C  Hum: %.0f%%  Pres: %.0f hPa  Alt: %.1f m\n",
                   temp, hum, pressure / 100, altitude);

    int bat_volt    = M5.Power.getBatteryVoltage();
    int bat_current = M5.Power.getBatteryCurrent();
    int bat_level   = M5.Power.getBatteryLevel();
    Serial.printf("Bat: %dmV  %dmA  %d%%\n", bat_volt, bat_current, bat_level);

    // Send to Grafana Cloud via InfluxDB line protocol
    HTTPClient http;
    http.begin("https://" + String(GC_USER) + ":" + String(GC_PASS) + "@" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write");
    http.addHeader("Content-Type", "text/plain");

    String postData = "m5ENVIII,location=" + String(LOCATION) +
        " temperature=" + String(temp, 2) +
        ",humidity="    + String(hum, 2) +
        ",pressure="    + String(pressure, 2) +
        ",altitude="    + String(altitude, 2) +
        ",bat_volt="    + String(bat_volt) +
        ",bat_current=" + String(bat_current) +
        ",bat_level="   + String(bat_level);

    int httpResponseCode = http.POST(postData);
    Serial.printf("HTTP: %d\n", httpResponseCode);
    http.end();

    // Deep sleep — wakes up and runs setup() fresh
    Serial.println("Entering deep sleep...");
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}
