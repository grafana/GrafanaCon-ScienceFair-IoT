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
