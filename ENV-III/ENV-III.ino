// ENV III Hat (SHT30 + QMP6988) — Temperature, Humidity, Pressure → Grafana Cloud
//
// Hardware: M5StickCPlus2 + ENV III Hat + 18650C Battery Hat
// Power strategy: Light sleep between readings keeps ~0.8mA draw so the
//   battery hat's low-current cutoff doesn't kill power (deep sleep does).
//   WiFi is only turned on to send, then immediately turned off.
//
// Dependent Libraries:
//   M5Unified     — https://github.com/m5stack/M5Unified
//   M5UnitENV     — https://github.com/m5stack/M5Unit-ENV

#include <M5Unified.h>
#include "M5UnitENV.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#include "config.h"

SHT3X sht3x;
QMP6988 qmp;

void lightSleep(uint64_t us) {
    esp_sleep_enable_timer_wakeup(us);
    esp_light_sleep_start();
}

bool connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++attempts > 20) {
            Serial.println("\nWiFi failed — will retry next cycle");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return false;
        }
    }
    Serial.println();
    Serial.printf("Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void wifiOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

    // Boot screen — only shown on first power-on
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
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.printf("Loc: %s", LOCATION);
    M5.Display.setCursor(10, 85);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.printf("Connecting...");

    // Init sensors — retry up to 3 times
    bool sensorsOk = false;
    for (int i = 0; i < 3 && !sensorsOk; i++) {
        if (i > 0) {
            Serial.printf("Sensor init retry %d/3\n", i + 1);
            delay(1000);
        }
        bool qmpOk = qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 0, 26, 400000U);
        bool shtOk = sht3x.begin(&Wire, SHT3X_I2C_ADDR, 0, 26, 400000U);
        sensorsOk = qmpOk && shtOk;
        if (!qmpOk) Serial.println("QMP6988 not found");
        if (!shtOk) Serial.println("SHT3X not found");
    }

    if (!sensorsOk) {
        M5.Display.setCursor(10, 110);
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.printf("Sensor FAIL");
        Serial.println("Sensors failed after retries — rebooting in 60s");
        delay(3000);
        M5.Display.fillScreen(BLACK);
        M5.Lcd.setBrightness(0);
        lightSleep(SLEEP_DURATION_US);
        ESP.restart();
    }

    bool wifiOk = connectWifi();

    M5.Display.setCursor(10, 110);
    if (wifiOk) {
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.printf("WiFi: OK");
    } else {
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.printf("WiFi: RETRY");
    }

    delay(2000);
    M5.Display.fillScreen(BLACK);
    M5.Lcd.setBrightness(0);

    wifiOff();
}

void loop() {
    delay(200);

    bool gotTemp     = sht3x.update();
    bool gotPressure = qmp.update();

    float temp     = 0.0;
    float hum      = 0.0;
    float pressure = 0.0;
    float altitude = 0.0;

    if (gotTemp) {
        temp = sht3x.cTemp;
        hum  = sht3x.humidity;
    }

    if (gotPressure) {
        pressure = qmp.calcPressure();
        altitude = qmp.altitude;
    }

    if (gotPressure && (pressure < 950 || pressure / 100 > 1200)) {
        Serial.printf("Bad pressure: %.0f — skipping cycle\n", pressure);
        lightSleep(SLEEP_DURATION_US);
        return;
    }

    if (!gotTemp && !gotPressure) {
        Serial.println("Sensors not ready — skipping cycle");
        lightSleep(SLEEP_DURATION_US);
        return;
    }

    int bat_volt  = M5.Power.getBatteryVoltage();
    int bat_level = M5.Power.getBatteryLevel();

    Serial.printf("T:%.1fC H:%.0f%% P:%.0fhPa A:%.1fm | Bat:%dmV %d%%\n",
                   temp, hum, pressure / 100, altitude, bat_volt, bat_level);

    // Turn WiFi on just for sending
    if (!connectWifi()) {
        lightSleep(SLEEP_DURATION_US);
        return;
    }

    int wifi_rssi = WiFi.RSSI();
    Serial.printf("RSSI: %ddBm\n", wifi_rssi);

    HTTPClient http;
    http.begin("https://" + String(GC_USER) + ":" + String(GC_PASS) + "@" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write");
    http.addHeader("Content-Type", "text/plain");

    String postData = "m5ENVIII,location=" + String(LOCATION) +
        " temperature=" + String(temp, 2) +
        ",humidity="    + String(hum, 2) +
        ",pressure="    + String(pressure, 2) +
        ",altitude="    + String(altitude, 2) +
        ",bat_volt="    + String(bat_volt) +
        ",bat_level="   + String(bat_level) +
        ",wifi_rssi="   + String(wifi_rssi);

    int httpResponseCode = http.POST(postData);
    Serial.printf("HTTP: %d\n", httpResponseCode);
    http.end();

    wifiOff();

    Serial.println("Light sleeping...");
    lightSleep(SLEEP_DURATION_US);
}
