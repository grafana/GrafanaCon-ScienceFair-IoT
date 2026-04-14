// Set your local WiFi username and password. Please use a 2.4GHz access point
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// For more information on where to get these values see: https://github.com/grafana/diy-iot/blob/main/README.md#sending-metrics
// NOTE: the hostname may say "prometheus-prod-…" or "influx-prod-…" —
// both point to the same Mimir backend and accept InfluxDB line protocol
// via /api/v1/push/influx/write.
#define GC_INFLUX_URL ""
#define GC_USER ""
#define GC_PASS ""

// Location tag — change per device before flashing (e.g. "lobby", "stage", "hallway_A")
#define LOCATION "booth"

// Deep sleep duration between readings (microseconds). 60 seconds = 60 * 1000000
#define SLEEP_DURATION_US (60 * 1000000)
