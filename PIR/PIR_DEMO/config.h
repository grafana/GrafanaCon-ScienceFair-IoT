// ── PIR Zone Counter: Demo ──────────────────────────────────────
// Place inside the booth near the demo table to count engaged visitors.

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

// Zone label for this sensor (used as Grafana label)
#define ZONE "demo"

// Location label (e.g. booth name or hall)
#define LOCATION "booth"
