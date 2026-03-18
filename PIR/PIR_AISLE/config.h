// ── PIR Zone Counter: Aisle ─────────────────────────────────────
// Place facing the conference aisle to count foot traffic near the booth.

// WiFi (2.4 GHz access point)
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Grafana Cloud – see https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
#define GC_INFLUX_URL ""  // e.g. "influx-prod-58-prod-eu-central-0.grafana.net"
#define GC_USER ""
#define GC_PASS ""

// Zone label for this sensor (used as Grafana label)
#define ZONE "aisle"

// Location label (e.g. booth name or hall)
#define LOCATION "booth"
