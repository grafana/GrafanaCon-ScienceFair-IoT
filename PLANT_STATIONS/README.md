# Plant Stations

Each station is a pair of M5StickC Plus2 devices:

- **RFID unit** — reads NFC tags, triggers watering via ESP-NOW, sends scan events to Grafana Cloud
- **WATERING unit** — controls a pump on command, sends moisture readings to Grafana Cloud

## Folder structure

```
PLANT_STATIONS/
├── RFID/              # Template sketch for the RFID unit
├── WATERING/          # Template sketch for the Watering unit
└── stations/
    ├── station1/      # Per-station copies with their own config
    ├── station2/
    └── station3/
```

## Setup a new station

1. Copy an existing `stations/stationN/` folder, rename to next number.
2. Edit `RFID/config.h` and `WATERING/config.h` inside it:
   - `WIFI_SSID` / `WIFI_PASSWORD` — 2.4 GHz network
   - `GC_INFLUX_URL`, `GC_USER`, `GC_PASS` — Grafana Cloud credentials
   - `PLANT_NAME` — label shown on LCD and in Grafana (e.g. "Fern", "Cactus")
   - `WATERING_MAC` — MAC of the paired watering unit (printed on its Serial/LCD at boot)
   - `WATER_DURATION_SEC` — pump run time per scan
3. Open the `RFID/` folder in Arduino IDE, select board **M5StickCPlus2**, flash.
4. Open the `WATERING/` folder, flash to the second device.
5. Power both on. RFID unit shows plant name and waits for NFC taps.

## Credentials

`config.h` files are gitignored. Blanked templates are committed — fill in locally before flashing.
