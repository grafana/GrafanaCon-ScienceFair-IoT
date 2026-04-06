# Booth Engagement Sensors

Foot traffic counting and engagement tracking for the GrafanaCon Science Fair booth.

## Sketches

| Folder | Sensor | Best for |
|--------|--------|----------|
| **PIR_AISLE** | Unit PIR | Aisle foot traffic (basic motion counter) |
| **PIR_DEMO** | Unit PIR | Booth presence + dwell time tracking |
| **TOF4M_AISLE** | Unit ToF4M | Aisle foot traffic (laser beam-break, more accurate) |

## Recommended combo

- **Aisle**: `TOF4M_AISLE` — point across the corridor, tape a white sheet on the opposite wall for best range.
- **Booth**: `PIR_DEMO` — faces the demo area, tracks engagement sessions and dwell time.

Both feed the same Grafana dashboard. The ToF4M sends the same `m5PIR` count metric as PIR_AISLE, so no query changes needed.

## Metrics

| Metric | Fields | Source |
|--------|--------|--------|
| `m5PIR` | `count` | All sketches — event count per zone |
| `m5ToF` | `distance`, `blocked` | TOF4M only — live beam distance (mm) and 0/1 state |

**Grafana panels:**
- `m5PIR_count` → **Stat** panel (total detections per zone)
- `m5ToF_distance` → **Time series** panel (live beam trace, dips = people)
- `m5ToF_blocked` → **State timeline** panel (0=clear/green, 1=blocked/red)
- Conversion rate: `m5PIR_count{zone="demo"} / m5PIR_count{zone="aisle"}`

## Setup

1. Open the sketch folder in Arduino IDE.
2. Copy `config.h` and fill in your credentials:
   - `WIFI_SSID` / `WIFI_PASSWORD` — 2.4 GHz network
   - `GC_INFLUX_URL`, `GC_USER`, `GC_PASS` — Grafana Cloud credentials
   - `ZONE` / `LOCATION` — labels for this sensor
   - (TOF4M only) `BEAM_ENTER_MM` / `BEAM_EXIT_MM` — set based on aisle width
3. Install libraries (Arduino Library Manager):
   - PIR sketches: `M5StickCPlus2`
   - TOF4M sketch: `M5StickCPlus2`, `M5Unit-ToF4M`
4. Select board **M5StickCPlus2**, flash.

## ToF4M tips

- Tape a **white piece of paper** on the opposite wall at sensor height — dramatically improves range.
- Set `BEAM_ENTER_MM` to ~85% of aisle width, `BEAM_EXIT_MM` to ~95%. The gap prevents flicker.
- The display shows live distance and beam status — useful for on-site calibration.

## Credentials

`config.h` files are gitignored. Templates with empty values are committed — fill in locally before flashing.
