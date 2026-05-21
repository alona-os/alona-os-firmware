# Temp/humidity ESP-NOW sensor node (Living Room MVP)

Structured ESP-IDF **sensor node** firmware: reads temperature/RH via `read_temperature_humidity()` (stub returns fixed fake values today), builds ESP-NOW **v1** JSON with **`alona_espnow_v1_build()`** from [`components/alona_protocol`](../../components/alona_protocol/), and sends every **5 seconds** to the gateway STA MAC on a fixed channel.

There is **no MQTT**, **no Wi-Fi association**, **no SNTP**, **no deep sleep**, and **no sensor driver** in this revision — same radio bootstrap pattern as [`examples/espnow_fake_node`](../espnow_fake_node/).

Wire contract: [`docs/esp32-espnow-v1.md`](../../docs/esp32-espnow-v1.md).

## Prerequisites

- ESP-IDF v5.1+ (`IDF_PATH`, `idf.py` on PATH)
- A running gateway ([`docs/gateway-setup.md`](../../docs/gateway-setup.md)) — note **STA MAC** and **Wi-Fi channel** from serial logs.

## Configure

Optional **single source**: edit **`property.local.json`** at the firmware repo root and run **`python3 scripts/sync_property_manifest.py`** — see [`scripts/README-property-manifest.md`](../../scripts/README-property-manifest.md).

If `main/alona_config.h` is missing and you are **not** using the manifest sync yet, **`idf.py build`** (first CMake configure) copies **`main/alona_config.h.example`** → `main/alona_config.h` automatically. You can still copy manually:

```bash
cd examples/temp_humidity_node
cp main/alona_config.h.example main/alona_config.h
```

Edit `main/alona_config.h`:

| Define | Set from gateway logs |
|--------|----------------------|
| `ALONA_WIFI_CHANNEL` | Line like `wifi channel=N (fake node must match this channel)` |
| `ALONA_GATEWAY_PEER_MAC_B0` … `B5` | Line `gateway sta mac aa:bb:...` — six hex bytes |

Defaults: `ALONA_DEVICE_ID` is `living-room-node-01`; send interval **5000 ms**.

## Build / flash / monitor

**ESP32-C3 Mini-1** (and other ESP32-C3 boards): IDF target is **`esp32c3`** — the module name does not change the target string.

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Classic ESP32 (Xtensa) devkits use **`esp32`** instead.

On macOS, ESP32-C3 boards with **USB-JTAG/serial** often show up as **`/dev/cu.usbmodem*`**; FTDI/USB‑UART adapters often **`/dev/cu.usbserial-*`**.

### Node logs

- `wifi channel fixed to N (no association)`
- `esp-now peer added (gateway)`
- `sending esp-now json len=...`
- `esp-now send ok to ...` or `esp-now send failed`

## Expected gateway logs

With MQTT connected and channel/MAC correct:

1. `esp-now rx len=... rssi=...`
2. `peer XX:XX:...` (your node STA MAC)
3. `decoded device_id=living-room-node-01 temp=yes rh=yes`
4. `mqtt publish ok msg_id=...`

## Verify MQTT

From any machine that reaches the broker:

```bash
mosquitto_sub -h <broker-LAN-ip> -p 1883 -t 'alona/esp32/living-room/telemetry' -v
```

You should see JSON including `"device_id":"living-room-node-01"` and `readings` with temperature and RH.

## Next steps (out of scope here)

- Implement `read_temperature_humidity()` with an I2C sensor (e.g. AHT20/SHT31).
- Optional: `measured_at`, power/deep sleep — not in this firmware revision.
