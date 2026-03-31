# GrafanaCon Science Fair - IoT

Code for the Grafana Science Fair IoT projects at GrafanaCon 2025 & 2026.

## Projects

| Folder | What it does |
|--------|-------------|
| `ENV-III/` | Temperature, humidity, pressure (ENV III Hat) - light sleep for battery operation |
| `Environmental/` | Temperature, humidity, pressure, CO2, VOC, lux (multi-sensor station) |
| `Plant_Stations/` | RFID + watering stations - NFC tap to water plants, metrics to Grafana |
| `PIR/` | Motion detection - foot traffic counter and dwell-time tracker |
| `EarthMoisture/` | Soil moisture sensor |
| `Heartrate/` | Heart rate monitor |
| `UltraSonic/` | Distance sensor |
| `dashboards/` | Grafana dashboard JSON exports (2025, 2026) |

## Drones (separate repo)

- [M5Stamp Drone Controller/Joystick](https://github.com/grafana/M5StampFlyController-GrafanaCon2025)
- [M5Stamp Drone Actual](https://github.com/grafana/M5StampFly-GrafanaCon2025)

## Hardware

Built on the **M5StickC Plus2** with various M5Stack sensor units, programmed via Arduino IDE.

## Getting started

1. Clone this repo
2. Open a project folder in Arduino IDE
3. Copy and fill in `config.h` with your WiFi and Grafana Cloud credentials
4. Select board **M5StickCPlus2**, flash

See individual project READMEs for details.
