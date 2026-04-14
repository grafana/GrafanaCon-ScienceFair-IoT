// ── ToF4M Aisle Counter ─────────────────────────────────────────
// Place pointing across the aisle to count foot traffic via beam-break.

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
#define ZONE "aisle"

// Location label (e.g. booth name or hall)
#define LOCATION "booth"

// Beam-break thresholds (mm). Aisle width is ~2000mm.
// ENTER: person detected when distance drops below this.
// EXIT:  person gone when distance rises above this.
// The gap prevents flickering at the boundary.
#define BEAM_ENTER_MM 1700
#define BEAM_EXIT_MM  1900

// Cooldown between counted events (ms). Prevents double-counting
// one person. ~800ms works for normal walking speed.
#define EVENT_COOLDOWN_MS 800
