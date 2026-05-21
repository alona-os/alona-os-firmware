# Alona OS — Firmware

Firmware for **ESP32** devices on the **alona-os system**:

- **Sensor nodes** — sample hardware, send telemetry over **ESP-NOW** to a gateway (see [`docs/esp32-espnow-v1.md`](docs/esp32-espnow-v1.md)).
- **Gateway ESP32** — receives ESP-NOW frames, publishes **MQTT JSON v1** to the Pi broker (see [`docs/esp32-mqtt-v1.md`](docs/esp32-mqtt-v1.md)).

**alona-os-core** ingests gateway MQTT only; nodes never talk to Mosquitto directly.

This repo is separate from application and host setup:

| Repo | Role |
|------|------|
| **alona-os-firmware** (this repo) | ESP32 node + gateway firmware — ESP-NOW, MQTT bridge (OTA planned) |
| **alona-os-core** | Elixir umbrella — Postgres, LiveView UI, `alona_ingest` adapters |
| **alona-os-infra** | Pi bootstrap — PostgreSQL, Mosquitto, env templates, systemd |

## Status

**Living Room MVP — gateway-first (ESP-IDF).**

| Piece | Location |
|-------|-----------|
| Shared JSON helpers (ESP-NOW v1 decode → MQTT v1 build) | [`components/alona_protocol/`](components/alona_protocol/) |
| Gateway (Wi-Fi STA + MQTT + ESP-NOW → queue → worker) | [`gateway/`](gateway/) |
| Bench fake node (fixed channel, **no** Wi-Fi association) | [`examples/espnow_fake_node/`](examples/espnow_fake_node/) |
| Temp/humidity node (structured; fake readings via `read_temperature_humidity()`, **`alona_espnow_v1_build()`**) | [`examples/temp_humidity_node/`](examples/temp_humidity_node/) |
| Operator guide (hardware, channel/MAC, verify) | [`docs/gateway-setup.md`](docs/gateway-setup.md) |
| **Property manifest** (Wi‑Fi / MQTT / ESP‑NOW → generated `alona_config.h`) | [`property.local.json.example`](property.local.json.example), [`scripts/README-property-manifest.md`](scripts/README-property-manifest.md) |

Wire contracts (locked for MVP):

| Doc | Hop |
|-----|-----|
| [`docs/esp32-espnow-v1.md`](docs/esp32-espnow-v1.md) | Sensor node → gateway (ESP-NOW) |
| [`docs/esp32-mqtt-v1.md`](docs/esp32-mqtt-v1.md) | Gateway → Pi (MQTT JSON v1) |

**Transport honesty:** ESP-NOW + MQTT QoS 0 here are **best-effort MVP** only — not a reliable delivery protocol. See [`docs/gateway-setup.md`](docs/gateway-setup.md).

Production sensor firmware (real sensors, pairing, power management) is **not** shipped yet; **`examples/temp_humidity_node`** sends fake temp/RH only (no sensor driver, no deep sleep); **`examples/espnow_fake_node`** remains a minimal ramping bench sender.

## Prerequisites

- **ESP-IDF v5.1+** (`idf.py`, `IDF_PATH`)
- Chip target must match **each** board you flash (`idf.py set-target …`):
  - **`esp32`** — classic ESP32 (Xtensa). Toolchain: `./install.sh esp32`
  - **`esp32c3`** — ESP32-C3 (RISC-V). Toolchain: `./install.sh esp32c3`

Install the toolchains you need — e.g. **`esp32c3` for the gateway** and **`esp32` for a classic ESP32 bench sender** (`./install.sh esp32c3` then `./install.sh esp32`, or `./install.sh esp32,esp32c3`).

If flashing fails with *“This chip is ESP32-C3, not ESP32”* (or the reverse), delete that project’s `build/`, remove stale `sdkconfig`, and run `set-target` for the correct SoC again.

## Property manifest — single source for firmware tuning

Keep **Wi‑Fi**, **MQTT**, and shared **ESP‑NOW** settings (`wifi_channel`, `gateway_sta_mac`) in **one gitignored file**: **`property.local.json`** at this repo root (copy from [`property.local.json.example`](property.local.json.example)), then run:

```bash
python3 scripts/sync_property_manifest.py
```

That regenerates **`gateway/main/alona_config.h`**, **`examples/temp_humidity_node/main/alona_config.h`**, and **`examples/espnow_fake_node/main/alona_config.h`** so every firmware image stays aligned. Full field reference: [`scripts/README-property-manifest.md`](scripts/README-property-manifest.md).

Pi / Phoenix MQTT env vars are **not** written automatically — the script prints matching **`export ALONA_MQTT_*`** lines for **`alona-os-core`** / **`alona-os-infra`** (`localhost` vs LAN IP differs by machine).

You can still maintain headers manually from each `*.example` file if you prefer.

## Quick start — gateway

```bash
cd gateway
cp main/alona_config.h.example main/alona_config.h
# edit Wi-Fi SSID/password, MQTT host/port/topic
# ALONA_MQTT_HOST = LAN IP of the machine running Mosquitto (see "MQTT broker" below), not 127.0.0.1

idf.py set-target esp32c3   # gateway reference build is ESP32-C3; only use esp32 here if your gateway silicon is classic ESP32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On macOS, serial ports are often `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.

Gateway logs print **STA MAC** and **Wi-Fi channel** after connect — required for the bench fake node.

## MQTT broker (dev)

Firmware uses **`mqtt://`** (plaintext on port **1883**).

- **`ALONA_MQTT_HOST`** must be reachable from Wi‑Fi: the broker machine’s **LAN IPv4** (e.g. `192.168.1.100`). **Do not use `localhost` / `127.0.0.1`** — that is wrong for another device on the network.
- The broker must **listen on the LAN**, not loopback-only. Example (macOS Homebrew): if `/opt/homebrew/etc/mosquitto/mosquitto.conf` has `listener 1883 127.0.0.1`, change to **`listener 1883 0.0.0.0`** and `brew services restart mosquitto`.

More detail and pitfalls: **[`docs/gateway-setup.md`](docs/gateway-setup.md)** (“Run Mosquitto”, troubleshooting).

## Quick start — bench fake node

See [`docs/gateway-setup.md`](docs/gateway-setup.md). Summary:

```bash
cd examples/espnow_fake_node
cp main/alona_config.h.example main/alona_config.h
# set ALONA_WIFI_CHANNEL to match gateway AP channel
# set ALONA_GATEWAY_PEER_MAC_B0..B5 to gateway STA MAC

idf.py set-target esp32   # must match **this sender** chip (often a classic ESP32 on the bench; use esp32c3 if your fake node board is C3)
idf.py build
idf.py -p /dev/ttyOTHER flash monitor
```

## Quick start — temp/humidity node (Living Room template)

Structured node firmware — fake fixed temp/RH every **5 s**, payloads built with **`alona_espnow_v1_build()`**. See **[`examples/temp_humidity_node/README.md`](examples/temp_humidity_node/README.md)**.

```bash
cd examples/temp_humidity_node
cp main/alona_config.h.example main/alona_config.h
# set ALONA_WIFI_CHANNEL and ALONA_GATEWAY_PEER_MAC_B0..B5 from gateway logs

idf.py set-target esp32c3   # ESP32-C3 Mini-1 and other C3 boards; use esp32 only for classic ESP32
idf.py build
idf.py -p /dev/ttyOTHER flash monitor
```

## Verify MQTT

Use your broker’s LAN IP (`ipconfig getifaddr en0` on Mac Wi‑Fi, etc.):

```bash
mosquitto_sub -h <broker-LAN-ip> -p 1883 -t 'alona/esp32/living-room/telemetry' -v
```

## Verify with `alona-os-core`

From the sibling umbrella repo: seeded DB + `mix phx.server` with MQTT pointed at the same broker. Details: `alona-os-core/apps/alona_ingest/README.md`.

## Role in the stack

```text
Sensor node(s)  --ESP-NOW-->  Gateway ESP32  --MQTT-->  Mosquitto (Pi)  -->  alona_ingest  -->  LiveView
                                                    ^
Cerbo GX (Victron) ---------------------------------|
```

Design goals: long uptime, fault isolation, low maintenance. Nodes stay off Wi-Fi/MQTT; gateways handle broker reconnect. Firmware stays aligned with the wire docs — not with UI or DB layout.

## Broker and ingest (gateway)

| Piece | Location | Notes |
|-------|----------|--------|
| MQTT broker | Pi — **alona-os-infra** | Default listener **1883** (`mosquitto/alona.conf`) |
| Ingest adapter | `alona-os-core/apps/alona_ingest` | `Esp32Adapter.normalize/1`; MQTT via `Mqtt.Handler` → `TopicRouter` |
| Measurement slugs | `alona-os-core` — `measurement_streams` | Living Room env: `env_living_temp_c`, `env_living_rh` |
| Devices in DB | `devices` table | Optional `firmware_version`, `last_seen_at` for ops |

Cerbo GX uses Venus MQTT and **`VictronAdapter`**; ESP32 gateways use a separate adapter path.

## Related code (core)

| Module | Purpose |
|--------|---------|
| `AlonaIngest.Adapters.Esp32Adapter` | Gateway MQTT JSON → v1 envelopes → measurement writes |
| `AlonaIngest.Mqtt.TopicRouter` | MVP: configured ESP32 topic(s) → `Esp32Adapter` |
| `AlonaIngest.Mqtt.Client` | Supervised `Tortoise311.Connection` (see `apps/alona_ingest/README.md`) |
| `AlonaCore.Measurements.Device` | Device metadata including `firmware_version` |

## Security

Default **alona-os-infra** Mosquitto config allows anonymous clients on the LAN — acceptable on the property LAN only. Before untrusted networks: TLS, credentials, firewall rules on the Pi broker, and ESP-NOW PMK/LMK rotation on nodes/gateways.

## License

TBD — align with sibling Alona OS repositories when this repo gains substantive content.
