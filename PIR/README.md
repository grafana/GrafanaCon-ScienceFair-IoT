# PIR Motion Sensors

Two PIR sketch variants for counting foot traffic and measuring engagement at the booth.

## Sketches

### PIR_AISLE — Motion counter
Simple event counter. Each motion detection increments a counter sent to Grafana Cloud.
Best for: doorways, aisles, high-traffic zones where you just need volume.

### PIR_DEMO — Dwell-time tracker
Tracks "presence sessions": a session starts on first motion and ends after a configurable
timeout with no motion. Sends visit count, dwell time (seconds), and a 10s heartbeat while
occupied. Best for: demo tables, stations where engagement duration matters.

**Conversion rate in Grafana:**
```
m5PIR_count{zone="demo"} / m5PIR_count{zone="aisle"}
```

## Hardware

- M5StickC Plus2
- Unit PIR on Grove Port A (signal on GPIO 33)

## Setup

1. Open the desired sketch folder in Arduino IDE.
2. Edit `config.h`:
   - `WIFI_SSID` / `WIFI_PASSWORD` — 2.4 GHz network
   - `GC_INFLUX_URL`, `GC_USER`, `GC_PASS` — Grafana Cloud credentials
   - `ZONE` — label for this sensor (e.g. "aisle", "demo", "entrance")
   - `LOCATION` — where it's deployed (e.g. "booth", "hall_A")
3. Select board **M5StickCPlus2**, flash.

## Credentials

`config.h` files are gitignored. Blanked templates are committed — fill in locally before flashing.
