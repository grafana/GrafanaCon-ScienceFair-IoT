// ── RFID Reader Config ──────────────────────────────────────────
// Fill in your credentials before uploading.

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

// Plant name – appears on the LCD and as a Grafana label
#define PLANT_NAME ""

// Watering duration (seconds)
#define WATER_DURATION_SEC 5
#define GOLDEN_DURATION_SEC 5

// Golden ticket UIDs (colon-separated hex, up to 3)
#define GOLDEN_UID_1 ""
#define GOLDEN_UID_2 ""
#define GOLDEN_UID_3 ""

// Paired watering unit MAC address
// The watering unit prints its MAC on Serial and LCD at boot.
// Convert  00:4B:12:C2:BE:EC  →  {0x00, 0x4B, 0x12, 0xC2, 0xBE, 0xEC}
#define WATERING_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
