// ── Station 1: Dracaena ─────────────────────────────────────────
// Open this folder in Arduino IDE and fill in your credentials before uploading.

// WiFi (2.4 GHz access point)
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Grafana Cloud – see https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
#define GC_INFLUX_URL ""  // e.g. "influx-prod-58-prod-eu-central-0.grafana.net"
#define GC_USER ""
#define GC_PASS ""

// LCD debug overlay
#define LCD_SHOW_DEBUG_INFO "1"

// ── Station-specific settings ───────────────────────────────────

#define PLANT_NAME "Dracaena"

// Pin assignments for the Watering Unit on Grove Port A
#define MOISTURE_PIN 33  // ADC input for moisture sensor
#define PUMP_PIN     32  // Digital output for pump control

// Moisture send interval (milliseconds)
#define MOISTURE_SEND_INTERVAL_MS 5000
