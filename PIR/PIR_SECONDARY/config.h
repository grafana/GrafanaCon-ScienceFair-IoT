// ── PIR Secondary (Sensor B) ────────────────────────────────────
// This device only sends ESP-NOW triggers to the Primary device.
// No WiFi or Grafana credentials needed.
// Place on the INNER (booth) side of the entrance.

// MAC address of the PRIMARY PIR device (Sensor A).
// The primary device prints its MAC on Serial and LCD at boot.
// Convert  AA:BB:CC:DD:EE:FF  →  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define PRIMARY_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
