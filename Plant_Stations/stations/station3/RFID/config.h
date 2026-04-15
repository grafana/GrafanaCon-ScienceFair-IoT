// ── Station 3 ──────────────────────────────────────────────────
// Open this folder in Arduino IDE and fill in your credentials before uploading.

// WiFi (2.4 GHz access point)
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Grafana Cloud – see https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
// NOTE: the hostname may say "prometheus-prod-…" or "influx-prod-…" —
// both point to the same Mimir backend and accept InfluxDB line protocol
// via /api/v1/push/influx/write.
#define GC_INFLUX_URL ""
#define GC_USER       ""
#define GC_PASS       ""

// LCD debug overlay
#define LCD_SHOW_DEBUG_INFO "1"

// ── Station-specific settings ───────────────────────────────────

#define PLANT_NAME "Plant3"

#define WATER_DURATION_SEC 5

// Set to {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} to broadcast to ALL nearby units.
#define WATERING_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Auto-write a URL to every scanned NFC sticker so phones open a webpage on tap.
// Comment out the line below to disable auto-writing.
#define NFC_WRITE_URL "https://play.grafana.org/d/anxh9qz/grafanacon2026-science-fair-iot"
