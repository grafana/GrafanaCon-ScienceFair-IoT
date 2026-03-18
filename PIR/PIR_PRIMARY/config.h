// ── PIR Primary (Sensor A) ──────────────────────────────────────
// This device connects to WiFi and sends booth traffic metrics to Grafana.
// Place on the OUTER (corridor) side of the booth entrance.

// WiFi (2.4 GHz access point)
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Grafana Cloud – see https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
#define GC_INFLUX_URL ""  // e.g. "influx-prod-58-prod-eu-central-0.grafana.net"
#define GC_USER ""
#define GC_PASS ""

// Location label for Grafana metrics (e.g. "booth-entrance", "hall-A")
#define LOCATION "booth"
