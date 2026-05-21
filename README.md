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
| Operator guide (hardware, channel/MAC, verify) | [`docs/gateway-setup.md`](docs/gateway-setup.md) |

Wire contracts (locked for MVP):

| Doc | Hop |
|-----|-----|
| [`docs/esp32-espnow-v1.md`](docs/esp32-espnow-v1.md) | Sensor node → gateway (ESP-NOW) |
| [`docs/esp32-mqtt-v1.md`](docs/esp32-mqtt-v1.md) | Gateway → Pi (MQTT JSON v1) |

**Transport honesty:** ESP-NOW + MQTT QoS 0 here are **best-effort MVP** only — not a reliable delivery protocol. See [`docs/gateway-setup.md`](docs/gateway-setup.md).

Production sensor firmware (real sensors, pairing, power management) is **not** this milestone; the fake node exists for bench testing only.

## Prerequisites

- **ESP-IDF v5.1+** (`idf.py`, `IDF_PATH`)
- Target **esp32** (`idf.py set-target esp32`)

## Quick start — gateway

```bash
cd gateway
cp main/alona_config.h.example main/alona_config.h
# edit Wi-Fi SSID/password, MQTT host/port/topic

idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On macOS, serial ports are often `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.

Gateway logs print **STA MAC** and **Wi-Fi channel** after connect — required for the bench fake node.

## Quick start — bench fake node

See [`docs/gateway-setup.md`](docs/gateway-setup.md). Summary:

```bash
cd examples/espnow_fake_node
cp main/alona_config.h.example main/alona_config.h
# set ALONA_WIFI_CHANNEL to match gateway AP channel
# set ALONA_GATEWAY_PEER_MAC_B0..B5 to gateway STA MAC

idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyOTHER flash monitor
```

## Verify MQTT

```bash
mosquitto_sub -h <broker-host> -p 1883 -t 'alona/esp32/living-room/telemetry' -v
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
