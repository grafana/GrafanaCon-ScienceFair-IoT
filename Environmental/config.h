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

// Measurement name and location tag for InfluxDB line protocol
#define MEASUREMENT_NAME "m5stick_env"
#define LOCATION "booth"
