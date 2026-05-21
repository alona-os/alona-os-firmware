# Gateway + bench fake node (ESP-IDF)

This document describes how to run the **Living Room MVP** bridge:

`ESP-NOW v1` (sensor/fake node → gateway) → `MQTT v1` (gateway → Mosquitto → `alona_ingest`).

## MVP transport disclaimer

This stack is a **best-effort MVP transport layer**, not a reliable delivery protocol:

- ESP-NOW has no application-level acknowledgements in this firmware.
- MQTT is published at **QoS 0**.
- The gateway applies only a **short dedupe window** for accidental duplicate frames (`device_id` + `measured_at`); duplicates outside that window or with different timestamps can still publish twice.
- There is **no** offline buffering if Wi-Fi or MQTT is down (frames are dropped or skipped with logs).

Production hardening (peer allowlists, encryption, MQTT QoS 1+, persistence) is future work.

## Hardware

- **Gateway:** ESP32 devkit (ESP-IDF target `esp32`).
- **Bench sender (optional):** second ESP32 running `examples/espnow_fake_node`.
- USB cables for serial flash/monitor.
- LAN access to a Mosquitto broker (Raspberry Pi from **alona-os-infra**, or local Mosquitto on a laptop).

Contracts (do not diverge without coordinated backend changes):

- [`esp32-espnow-v1.md`](esp32-espnow-v1.md)
- [`esp32-mqtt-v1.md`](esp32-mqtt-v1.md)

## Prerequisites

- **ESP-IDF v5.1+** installed and environment sourced (`IDF_PATH`, `idf.py` on `PATH`).
- Wi-Fi credentials for the gateway station interface.
- MQTT broker reachable at `ALONA_MQTT_HOST`:`ALONA_MQTT_PORT` (default dev: `1883`).

## Configure gateway

```bash
cd gateway
cp main/alona_config.h.example main/alona_config.h
# edit main/alona_config.h — SSID, password, MQTT host/port/topic
```

Build / flash / monitor:

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On macOS, serial ports often look like `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.

### Serial logs to expect (gateway)

- Boot banner with gateway label (`ALONA_GATEWAY_DEVICE_ID` is **not** sent on MQTT).
- **Wi-Fi** connected / disconnected.
- **Wi-Fi channel** after association — needed for the fake node.
- **Gateway STA MAC** — give this to the fake node as the ESP-NOW peer.
- **ESP-NOW** receive lines (length, RSSI, peer MAC).
- Decoded readings summary and MQTT publish success/failure.

## Configure bench fake node (no Wi-Fi join)

The fake node does **not** call `esp_wifi_connect()`. It sets a fixed **`esp_wifi_set_channel()`** only.

You **must** set `ALONA_WIFI_CHANNEL` to match the **gateway’s Wi-Fi channel** (printed by the gateway after it gets an IP). If the channel is wrong, ESP-NOW frames will not reach the gateway.

```bash
cd examples/espnow_fake_node
cp main/alona_config.h.example main/alona_config.h
# set ALONA_WIFI_CHANNEL and ALONA_GATEWAY_PEER_MAC_B0..B5 from gateway logs
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyOTHER flash monitor
```

## Verify MQTT on the broker

On any machine that reaches the broker:

```bash
mosquitto_sub -h <broker-host> -p 1883 -t 'alona/esp32/living-room/telemetry' -v
```

You should see JSON like:

```json
{"version":1,"device_id":"living-room-esp32","readings":{"temperature_c":23.00,"relative_humidity_pct":54.30},"rssi_dbm":-42}
```

(`device_id` is whatever the node sent — the gateway does not rename it.)

## Verify through `alona-os-core`

No firmware repo changes are required in core; ingest already subscribes to `alona/esp32/living-room/telemetry` by default.

1. Ensure Postgres is migrated/seeded so streams `env_living_temp_c` and `env_living_rh` exist.
2. Run Mosquitto where core expects it (`ALONA_MQTT_HOST` / `ALONA_MQTT_PORT`).
3. From `alona-os-core/`: `./setup.sh` (if needed), then `mix phx.server` with MQTT enabled (not `ALONA_MQTT_ENABLED=false`).
4. Open `/environment` or `/` and confirm live updates, or use logs.

See [`../alona-os-core/apps/alona_ingest/README.md`](../../alona-os-core/apps/alona_ingest/README.md) for env vars and a `mosquitto_pub` sanity check.

## Troubleshooting

| Symptom | Things to check |
|--------|-------------------|
| No ESP-NOW on gateway | Fake node **channel** matches gateway AP channel; peer MAC matches gateway **STA** MAC; both boards powered; antennas |
| Gateway MQTT never connects | Broker IP/port; firewall; anonymous MQTT allowed on LAN (default infra) |
| `mqtt not connected, skip publish` | Wait for Wi-Fi + MQTT; gateway logs MQTT events |
| `rx queue full` | Bursty traffic — increase `ALONA_ESPNOW_RX_QUEUE_DEPTH` or slow senders |
| Core shows no ingest | Topic spelling; `ALONA_MQTT_TOPICS` must include living-room topic if overridden |
| `no_mappable_readings` in logs | ESP-NOW JSON missing numeric `temperature_c` / `relative_humidity_pct` |

## Manual test without the fake node project

Any ESP32 that transmits **valid ESP-NOW v1 JSON** (≤250 bytes) to the gateway MAC on the correct channel will exercise the same path. Use the sample JSON in [`esp32-espnow-v1.md`](esp32-espnow-v1.md).
