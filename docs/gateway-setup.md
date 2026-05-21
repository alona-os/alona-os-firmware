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

- **Gateway:** ESP32 family devkit (**ESP-IDF target `esp32c3`** for the reference gateway build — use `esp32` only if your gateway MCU is classic ESP32).
- **Bench sender (optional):** second board running `examples/espnow_fake_node`; target match that board (`esp32` common for a classic ESP32 bench node).
- USB cables for serial flash/monitor.
- LAN access to a Mosquitto broker (Raspberry Pi from **alona-os-infra**, or local Mosquitto on a laptop).

Contracts (do not diverge without coordinated backend changes):

- [`esp32-espnow-v1.md`](esp32-espnow-v1.md)
- [`esp32-mqtt-v1.md`](esp32-mqtt-v1.md)

## Prerequisites

- **ESP-IDF v5.1+** installed and environment sourced (`IDF_PATH`, `idf.py` on `PATH`).
- Wi-Fi credentials for the gateway station interface.
- Mosquitto (or compatible broker) reachable from the **gateway’s Wi‑Fi** — see **[Run Mosquitto](#run-mosquitto-plaintext-mqtt)**. Set **`ALONA_MQTT_HOST`** / **`ALONA_MQTT_PORT`** (default **1883**) in [`gateway/main/alona_config.h`](../gateway/main/alona_config.h.example) after copying the example file.

## Run Mosquitto (plaintext MQTT)

Gateways use **`mqtt://` only** — no TLS in this MVP firmware.

### LAN IP (`ALONA_MQTT_HOST`)

Use the IPv4 address of the machine running Mosquitto **as seen from other Wi‑Fi clients** (often `192.168.x.x`).

- Never use **`127.0.0.1`** or **`localhost`** in `alona_config.h` — that would point the ESP at itself.

### Listener must bind to LAN (not only loopback)

If Mosquitto listens only on **`127.0.0.1`**, nothing accepts MQTT on **`192.168.x.x:1883`**. The gateway may log **`delayed connect error: Connection reset by peer`**, MQTT transport failures, then disconnect.

Use a **`listener`** on all interfaces (**LAN only**, dev convenience):

```conf
listener 1883 0.0.0.0
allow_anonymous true
```

### macOS (Homebrew)

1. **`brew install mosquitto`** (if needed).

2. Edit **`/opt/homebrew/etc/mosquitto/mosquitto.conf`** (install prefix may vary; **`brew --prefix mosquitto`** shows it).

3. Replace **`listener 1883 127.0.0.1`** (default) with **`listener 1883 0.0.0.0`** and keep **`allow_anonymous true`** for local dev.

4. **`brew services restart mosquitto`**

5. Wi‑Fi IP (example): **`ipconfig getifaddr en0`** → put that value in **`ALONA_MQTT_HOST`**.

6. Test subscription (use a concrete topic first; **`#`** alone can error on some CLI builds):

```bash
mosquitto_sub -h 192.168.1.REPLACE_ME -p 1883 -t test -v
```

### Raspberry Pi

Match **alona-os-infra** Mosquitto setup; confirm the listener accepts connections from LAN clients, not only loopback.

## Configure gateway

```bash
cd gateway
cp main/alona_config.h.example main/alona_config.h
# edit SSID/password; ALONA_MQTT_HOST = Mosquitto machine LAN IP (not 127.0.0.1); port usually 1883
```

Build / flash / monitor:

```bash
idf.py set-target esp32c3   # gateway reference is ESP32-C3; use esp32 for a classic ESP32 gateway
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
idf.py set-target esp32   # match sender chip (often classic ESP32; use esp32c3 if sender is C3)
idf.py build
idf.py -p /dev/ttyOTHER flash monitor
```

## Verify MQTT on the broker

On any machine that reaches the broker:

```bash
mosquitto_sub -h <broker-LAN-ip> -p 1883 -t 'alona/esp32/living-room/telemetry' -v
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
| Gateway MQTT errors / **`Connection reset by peer`** (`esp-tls`, transport connect) | **Wrong `ALONA_MQTT_HOST`** (never `127.0.0.1`); broker not reachable on LAN; **`listener`** bound only to **`127.0.0.1`** — use **`listener 1883 0.0.0.0`**; firewall; TLS-only broker (firmware expects plain **`mqtt://`** on **1883**) |
| Gateway MQTT never connects | Broker IP/port; firewall; anonymous MQTT allowed on LAN (default infra) |
| `mqtt not connected, skip publish` | Wait for Wi-Fi + MQTT; gateway logs MQTT events |
| `rx queue full` | Bursty traffic — increase `ALONA_ESPNOW_RX_QUEUE_DEPTH` or slow senders |
| Core shows no ingest | Topic spelling; `ALONA_MQTT_TOPICS` must include living-room topic if overridden |
| `no_mappable_readings` in logs | ESP-NOW JSON missing numeric `temperature_c` / `relative_humidity_pct` |

## Manual test without the fake node project

Any ESP32 that transmits **valid ESP-NOW v1 JSON** (≤250 bytes) to the gateway MAC on the correct channel will exercise the same path. Use the sample JSON in [`esp32-espnow-v1.md`](esp32-espnow-v1.md).
