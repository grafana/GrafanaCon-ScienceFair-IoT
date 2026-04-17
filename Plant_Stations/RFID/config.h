// ── RFID Reader Config ──────────────────────────────────────────
// Fill in your credentials before uploading.

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

// Plant name for this station -- used as Grafana label and on the LCD
#define PLANT_NAME "YourPlant"

// Watering duration (seconds) for all tags (including golden tickets)
#define WATER_DURATION_SEC 5

// MAC address of the Watering unit StickC Plus2.
// The watering unit prints its MAC on Serial and LCD at boot.
// Convert  AA:BB:CC:DD:EE:FF  →  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
//
// Uncomment the line below to broadcast to ALL nearby watering units:
// #define WATERING_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define WATERING_MAC {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// Auto-write a URL to every scanned NFC sticker so phones open a webpage on tap.
// The RFID unit writes the URL once; stickers that already have it are skipped.
// Comment out the line below to disable auto-writing.
// Tracked URL follows Grafana's internal tracking-URL schema:
//   src  = Marketing Source   (required)
//   camp = Marketing Campaign (required)
#define NFC_WRITE_URL "https://play.grafana.org/d/anxh9qz/grafanacon26-iot?src=nfc-sticker&camp=grafanacon-science-fair-iot"
