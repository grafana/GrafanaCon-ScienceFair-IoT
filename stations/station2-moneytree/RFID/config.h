// ── Station 2: Money Tree ───────────────────────────────────────
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

#define PLANT_NAME "Money Tree"

#define WATER_DURATION_SEC 5
#define GOLDEN_DURATION_SEC 5

// Golden ticket UIDs (shared across all stations)
#define GOLDEN_UID_1 ""
#define GOLDEN_UID_2 ""
#define GOLDEN_UID_3 ""

// Paired watering unit MAC address
// The watering unit prints its MAC on Serial and LCD at boot.
// Convert  AA:BB:CC:DD:EE:FF  →  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
//
// Uncomment the line below to broadcast to ALL nearby watering units:
// #define WATERING_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define WATERING_MAC {0xF0, 0x24, 0xF9, 0x97, 0x5E, 0xC4}
