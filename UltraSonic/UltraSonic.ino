// Written for Grafana Labs to demonstrate how to use the M5Stick CPlus2 with Grafana Cloud at GrafanaCon 2025
// 2025/04/25
// Willie Engelbrecht - willie.engelbrecht@grafana.com
// Introduction to time series: https://grafana.com/docs/grafana/latest/fundamentals/timeseries/
// M5StickCPlus2: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
// Register for a free Grafana Cloud account including free metrics and logs: https://grafana.com
// NOTE: This sketch targets the M5StickC Plus (v1), not the Plus2. For Plus2, swap M5StickCPlus.h for M5StickCPlus2.h and TFT_eSprite for M5Canvas.

#include <M5StickCPlus.h>
#include <WiFi.h>
#include <Wire.h>
#include <Unit_Sonic.h>
#include <HTTPClient.h>

#include "config.h"


TFT_eSprite Disbuff = TFT_eSprite(&M5.Lcd);
SONIC_I2C sensor;

#define MAX_BRIGHTNESS 255
#define bufferLength   100
const byte Button_A = 37;
const byte pulseLED = 26;

float irBuffer[bufferLength];
float redBuffer[bufferLength];

int8_t V_Button, flag_Reset;
int32_t spo2, heartRate, old_spo2;
int8_t validSPO2, validHeartRate;
const byte RATE_SIZE = 5;
uint16_t rate_begin  = 0;
uint16_t rates[RATE_SIZE];
byte rateSpot = 0;
float beatsPerMinute;
int beatAvg;
byte num_fail;

uint16_t line[2][320] = {0};

uint32_t red_pos = 0, ir_pos = 0;
uint16_t ir_max = 0, red_max = 0, ir_min = 0, red_min = 0, ir_last = 0,
         red_last    = 0;
uint16_t ir_last_raw = 0, red_last_raw = 0;
uint16_t ir_disdata, red_disdata;
uint16_t Alpha = 0.3 * 256;
uint32_t t1, t2, last_beat, Program_freq;


TaskHandle_t WiFiTaskHandle = NULL;
QueueHandle_t WiFiQueue;

static unsigned long lastSendTime = 0;

struct data_to_send{
    float distance;
};

void callBack(void) {
    V_Button = digitalRead(Button_A);
    if (V_Button == 0) flag_Reset = 1;
    delay(10);
}

void setup() {
    Serial.begin(115200); 

    // init
    M5.begin();                 // initialize M5StickCPlus

    // set Lcd
    M5.Lcd.setRotation(3);
    M5.Lcd.setSwapBytes(false);
    Disbuff.createSprite(240, 135);
    Disbuff.setSwapBytes(true);
    Disbuff.createSprite(240, 135);

    // initialize Sensor
    sensor.begin();

    data_to_send d = {0};
    WiFiQueue = xQueueCreate(1, sizeof(d));
    if (WiFiQueue == NULL) {
        Serial.println("Error creating queue!");
        return;
    }

    initWifi();
    
    xTaskCreate(
        sendHttpPost,    // Function
        "sendHttpPost",  // Task name
        16384,           // Stack size
        NULL,            // Task parameters
        1,               // Priority
        &WiFiTaskHandle  // Task handle
    );
}

void sendHttpPost(void *parameter) {
    data_to_send message;

    HTTPClient http;
               
    http.begin("https://" + String(GC_USER) + ":" + String(GC_PASS) + "@" + String(GC_INFLUX_URL) + "/api/v1/push/influx/write");  // Specify the URL
    http.addHeader("Content-Type", "text/plain");

    while (true) {
        // Wait for data from the queue (Blocks until data arrives)
        if (xQueueReceive(WiFiQueue, &message, portMAX_DELAY)) {
            if (WiFi.status() == WL_CONNECTED) {
              
                String postData = "";
                postData = "m5UltraSonic,location=home distance=" + String(message.distance);

                int httpResponseCode = http.POST(postData);

              //  http.end();
            }
        }
        delay(50);
    }

}

void initWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  int wifi_loop_count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // Restart the device if we can't connect to WiFi after 2 minutes
    wifi_loop_count += 1;
    if (wifi_loop_count > 40) {
      ESP.restart();
    }
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
    // put your main code here, to run repeatedly:
    static float red = 0.0;
    
    red = sensor.getDistance();
    Serial.println("Distance measured: " + String(red));

    if (millis() - lastSendTime >= 1000) {
        lastSendTime = millis();
        data_to_send d = {red};

        xQueueOverwrite(WiFiQueue, &d);
    }

    if ((red > 20) && (red < 4000)) {
        redBuffer[(red_pos + 100) % 100] = (int)round(red);
    }

    line[0][(red_pos + 240) % 320] = (red_last_raw * (256 - Alpha) + red * Alpha) / 256;

    red_last_raw = line[0][(red_pos + 240) % 320];
    red_pos++;

    for (int i = 0; i < 240; i++) {
        if (i == 0) {
            red_max = red_min = line[0][(red_pos + i) % 320];
        } else {
            red_max = (line[0][(red_pos + i) % 320] > red_max) ? line[0][(red_pos + i) % 320] : red_max; 
            red_min = (line[0][(red_pos + i) % 320] < red_min) ? line[0][(red_pos + i) % 320] : red_min;
        }

    }

    Disbuff.fillRect(0, 0, 240, 135, BLACK);

    for (int i = 0; i < 240; i++) {
        red_disdata = map(line[0][(red_pos + i) % 320], red_max, red_min, 0, 135);
        Disbuff.drawLine(i, red_last, i + 1, red_disdata, GREEN);
        red_last = red_disdata;
    }

    Disbuff.setTextSize(2);
    Disbuff.setTextColor(GREEN);
    Disbuff.setCursor(5, 5);
    Disbuff.printf("Dist:%0.2fmm", red);

    Disbuff.pushSprite(0, 0);
}