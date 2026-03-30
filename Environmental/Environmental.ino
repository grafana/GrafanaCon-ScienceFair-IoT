// Written for Grafana Labs to demonstrate how to use the M5Stick CPlus2 with Grafana Cloud at GrafanaCon 2025
// 2025/04/25
// Willie Engelbrecht - willie.engelbrecht@grafana.com
// Introduction to time series: https://grafana.com/docs/grafana/latest/fundamentals/timeseries/
// M5StickCPlus2: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
// Register for a free Grafana Cloud account including free metrics and logs: https://grafana.com

#include "M5Unified.h"
#include "M5UnitENV.h"          // Temp, Humidity, Pressure
#include "Adafruit_SGP30.h"     // VOC, CO2
#include <M5_DLight.h>          // Light level
#include <Unit_Sonic.h>         // Sonic (Distance) sensor

// ===================================================
// All the things that needs to be changed 
// Your local WiFi details
// Your Grafana Cloud details
// ===================================================
#include "config.h"
#include "utility.h"

// ===================================================
// Includes - Needed to write Prometheus or Loki metrics/logs to Grafana Cloud
// No need to change anything here
// ===================================================
#include "certificates.h"
#include <PromLokiTransport.h>
#include <PrometheusArduino.h>

// ===================================================
// Global Variables
// ===================================================
SHT3X sht30;              // temperature and humidity sensor
QMP6988 qmp6988;          // pressure sensor
Adafruit_SGP30 sgp;       // VOC/CO2 sensor
M5_DLight dlight;         // Light sensor
SONIC_I2C sonic;          // Sonic (distance) sensor

float temp     = 0.0;
float hum      = 0.0;
float pressure = 0.0;

int voc      = 0;
int co2      = 0;

uint16_t lux      = 0;

float distance = 0.0;

// Include for I2C Multiplexer
#define PAHUB2_ADDRESS 0x70

// Client for Prometheus metrics
PromLokiTransport transport;
PromClient client(transport);

// Create a write request for 11 time series.
WriteRequest req(9, 2048);

// Define all our timeseries
TimeSeries ts_m5stick_temperature(1, "m5stick_temp", PROM_LABELS);
TimeSeries ts_m5stick_humidity(1, "m5stick_hum", PROM_LABELS);
TimeSeries ts_m5stick_pressure(1, "m5stick_pressure", PROM_LABELS);
TimeSeries ts_m5stick_bat_volt(1, "m5stick_bat_volt", PROM_LABELS);
TimeSeries ts_m5stick_bat_current(1, "m5stick_bat_current", PROM_LABELS);
TimeSeries ts_m5stick_bat_level(1, "m5stick_bat_level", PROM_LABELS);
TimeSeries ts_m5stick_voc(1, "m5stick_voc", PROM_LABELS);
TimeSeries ts_m5stick_co2(1, "m5stick_co2", PROM_LABELS);
TimeSeries ts_m5stick_lux(1, "m5stick_lux", PROM_LABELS);

void setup() {
    Serial.begin(115200);
    Serial.println("Booting up!");

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(3);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.printf("== Grafana Labs ==");
    M5.Display.setTextColor(WHITE, BLACK);

    Wire.begin();       // Wire init, adding the I2C bus. 
    if (!qmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 32, 33, 400000U)) {
        Serial.println("Couldn't find QMP6988");
    }
    else {
      Serial.println("Found Pressure sensor!");
    }
    if (!sht30.begin(&Wire, SHT3X_I2C_ADDR, 32, 33, 400000U)) {
        Serial.println("Couldn't find SHT3X");
    }
    else {
      Serial.println("Found Temp/Humidity sensor!");
    }
    if (!sgp.begin()) {  // Init the sensor
        Serial.println("VOC/CO2 Sensor not found!");
    }
    else {
      Serial.println("Found VoC/CO2 Sensor!");
    }

    // Initialize the Lux sensor
    dlight.begin();
    dlight.setMode(CONTINUOUSLY_H_RESOLUTION_MODE2);

    M5.Display.setCursor(10, 30);
    M5.Display.printf("Hello, %s", YOUR_NAME);
    M5.Display.setCursor(10, 60);
    M5.Display.printf("Please wait:\r\n Connecting to WiFi");

    // Connecting to Wifi
    transport.setUseTls(true);
    transport.setCerts(grafanaCert, strlen(grafanaCert));
    transport.setWifiSsid(WIFI_SSID);
    transport.setWifiPass(WIFI_PASSWORD);
    transport.setDebug(Serial);  // Remove this line to disable debug logging of the client.
    if (!transport.begin()) {
        Serial.println(transport.errmsg);
        while (true) {};        
    }

    M5.Display.setCursor(10, 105);
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.printf("Connected!");
    delay(1500); 

    // Configure the Grafana Cloud client
    client.setUrl(GC_URL);
    client.setPath((char*)GC_PATH);
    client.setPort(GC_PORT);
    client.setUser(GC_USER);
    client.setPass(GC_PASS);
    client.setDebug(Serial);  // Remove this line to disable debug logging of the client.
    if (!client.begin()) {
        Serial.println(client.errmsg);
        while (true) {};
    }

    // Add our TimeSeries to the WriteRequest
    req.addTimeSeries(ts_m5stick_temperature);
    req.addTimeSeries(ts_m5stick_humidity);
    req.addTimeSeries(ts_m5stick_pressure);
    req.addTimeSeries(ts_m5stick_bat_volt);
    req.addTimeSeries(ts_m5stick_bat_current);
    req.addTimeSeries(ts_m5stick_bat_level);
    req.addTimeSeries(ts_m5stick_voc);
    req.addTimeSeries(ts_m5stick_co2);
    req.addTimeSeries(ts_m5stick_lux);
    Serial.println("End of setup()");
}

void loop() {
    unsigned long loopStart = millis();

    int64_t time;
    time = transport.getTimeMillis();
    Serial.printf("\r\n====================================\r\n");

    if (qmp6988.update()) {
        pressure = qmp6988.calcPressure();
    }
    if (sht30.update()) {
        temp = sht30.cTemp;
        hum  = sht30.humidity;
    } else {
        temp = 0, hum = 0;
    }
    Serial.printf("Temp: %2.1f °C \r\nHumidity: %2.0f%%  \r\nPressure:%2.0f hPa\r\n\n", temp, hum, pressure / 100);

    if (!sgp.IAQmeasure()) {
        Serial.println("eCO2/VOC Measurement failed!");
    }
    voc = int(sgp.TVOC);
    co2 = int(sgp.eCO2);
    Serial.printf("eCO2: %d ppm\nTVOC: %d ppb\n\n", co2, voc);

    lux = dlight.getLUX();
    Serial.printf("Lux (light level): %d Lux\n\n", lux);

    int bat_volt = M5.Power.getBatteryVoltage();
    Serial.printf("Battery Volt: %dmv \n", bat_volt);

    int bat_current = M5.Power.getBatteryCurrent();
    Serial.printf("Battery Current: %dmv \n", bat_current);

    int bat_level = M5.Power.getBatteryLevel();
    Serial.printf("Battery Level: %d%%\n\n", bat_level);

    ts_m5stick_temperature.addSample(time, temp);
    ts_m5stick_humidity.addSample(time, hum);
    ts_m5stick_pressure.addSample(time, pressure);
    ts_m5stick_voc.addSample(time, int(voc));
    ts_m5stick_co2.addSample(time, int(co2));
    ts_m5stick_lux.addSample(time, lux);
    ts_m5stick_bat_volt.addSample(time, bat_volt);
    ts_m5stick_bat_current.addSample(time, bat_current);
    ts_m5stick_bat_level.addSample(time, bat_level);

    // Check WiFi before attempting send
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost — reconnecting");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
            delay(500);
            retries++;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi reconnect failed — skipping send");
            ts_m5stick_temperature.resetSamples();
            ts_m5stick_humidity.resetSamples();
            ts_m5stick_pressure.resetSamples();
            ts_m5stick_bat_volt.resetSamples();
            ts_m5stick_bat_current.resetSamples();
            ts_m5stick_bat_level.resetSamples();
            ts_m5stick_voc.resetSamples();
            ts_m5stick_co2.resetSamples();
            ts_m5stick_lux.resetSamples();
            delay(2000);
            return;
        }
    }

    unsigned long sendStart = millis();
    PromClient::SendResult res = client.send(req);
    unsigned long sendMs = millis() - sendStart;
    Serial.printf("Prom send took %lums\n", sendMs);

    ts_m5stick_temperature.resetSamples();
    ts_m5stick_humidity.resetSamples();
    ts_m5stick_pressure.resetSamples();
    ts_m5stick_bat_volt.resetSamples();
    ts_m5stick_bat_current.resetSamples();
    ts_m5stick_bat_level.resetSamples();
    ts_m5stick_voc.resetSamples();
    ts_m5stick_co2.resetSamples();
    ts_m5stick_lux.resetSamples();

    M5.Lcd.fillRect(0, 0, 256, 256, BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.printf("== Grafana Labs ==");
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(0, 40);
    M5.Display.printf(" Temp: %2.1f  \r\n Humi: %2.0f%%  \r\n Pres:%2.0f hPa\r\n", temp, hum, pressure / 100);
    M5.Display.printf(" VOC:%d CO2:%d\r\n LUX:%d\r\n", voc, co2, lux);

    unsigned long loopMs = millis() - loopStart;
    Serial.printf("Loop total: %lums (send: %lums)\n", loopMs, sendMs);

    unsigned long startTime = millis();
    while (millis() - startTime < 1000) {
        check_buttonA(res);
    }
}
