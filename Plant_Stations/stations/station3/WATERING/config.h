// ── Station 3 ───────────────────────────────────────────────────
// Open this folder in Arduino IDE and fill in your credentials before uploading.

// WiFi (2.4 GHz access point)
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Grafana Cloud – see https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
// NOTE: the hostname may say "prometheus-prod-…" or "influx-prod-…" —
// both point to the same Mimir backend and accept InfluxDB line protocol
// via /api/v1/push/influx/write.
#define GC_INFLUX_URL ""
#define GC_USER ""
#define GC_PASS ""

// LCD debug overlay
#define LCD_SHOW_DEBUG_INFO "1"

// ── Station-specific settings ───────────────────────────────────

// TODO: Set the plant name for this station
#define PLANT_NAME ""

// Pin assignments for the Watering Unit on Grove Port A
#define MOISTURE_PIN 33  // ADC input for moisture sensor
#define PUMP_PIN     32  // Digital output for pump control

// Moisture send interval (milliseconds)
#define MOISTURE_SEND_INTERVAL_MS 5000
