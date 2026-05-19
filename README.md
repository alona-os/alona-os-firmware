# Alona OS — Firmware

Firmware for **ESP32** devices on the **alona-os system**:

- **Sensor nodes** — sample hardware, send telemetry over **ESP-NOW** to a gateway (see [`docs/esp32-espnow-v1.md`](docs/esp32-espnow-v1.md)).
- **Gateway ESP32** — receives ESP-NOW frames, publishes **MQTT JSON v1** to the Pi broker (see [`docs/esp32-mqtt-v1.md`](docs/esp32-mqtt-v1.md)).

**alona-os-core** ingests gateway MQTT only; nodes never talk to Mosquitto directly.

This repo is separate from application and host setup:

| Repo | Role |
|------|------|
| **alona-os-firmware** (this repo) | ESP32 node + gateway firmware — sensors, ESP-NOW, MQTT bridge, OTA (planned) |
| **alona-os-core** | Elixir umbrella — Postgres, LiveView UI, `alona_ingest` adapters |
| **alona-os-infra** | Pi bootstrap — PostgreSQL, Mosquitto, env templates, systemd |

## Status

**Early stage.** No firmware source in this repository yet. Wire contracts are documented and locked for the Living Room MVP:

| Doc | Hop |
|-----|-----|
| [`docs/esp32-espnow-v1.md`](docs/esp32-espnow-v1.md) | Sensor node → gateway (ESP-NOW) |
| [`docs/esp32-mqtt-v1.md`](docs/esp32-mqtt-v1.md) | Gateway → Pi (MQTT JSON v1) |

Backend ingest already accepts MQTT v1 on the configured topic via `Esp32Adapter`.

## Role in the stack

```text
Sensor node(s)  --ESP-NOW-->  Gateway ESP32  --MQTT-->  Mosquitto (Pi)  -->  alona_ingest  -->  LiveView
                                                    ^
Cerbo GX (Victron) ---------------------------------|
```

Design goals: long uptime, fault isolation, low maintenance. Nodes stay off Wi-Fi/MQTT; gateways handle broker reconnect. Firmware must not couple to UI or database details — only the documented wire contracts matter.

## Broker and ingest (gateway)

| Piece | Location | Notes |
|-------|----------|--------|
| MQTT broker | Pi — **alona-os-infra** | Default listener **1883** (`mosquitto/alona.conf`) |
| Ingest adapter | `alona-os-core/apps/alona_ingest` | `Esp32Adapter.normalize/1`; MQTT via `Mqtt.Handler` → `TopicRouter` |
| Measurement slugs | `alona-os-core` — `measurement_streams` | Stable slugs; UI reads via `Measurements.streams_for_slugs/1` |
| Devices in DB | `devices` table | Optional `firmware_version`, `last_seen_at` for ops |

Gateway broker URL: `ALONA_MQTT_HOST` / `ALONA_MQTT_PORT` in `/etc/alona/alona.env` (see **alona-os-infra**). Point gateways at the Pi LAN address on port **1883** unless you add TLS/auth.

Cerbo GX uses Venus MQTT and **`VictronAdapter`**; ESP32 gateways use a separate adapter path.

## Development (when firmware lands)

1. **Local dev** — `alona-os-core`: `./setup.sh`, `mix phx.server` (seeds supply demo measurements; MQTT not required for UI work).
2. **MQTT on laptop** — run Mosquitto locally or set `ALONA_MQTT_*` when testing gateway → ingest.
3. **Pi host** — **alona-os-infra** `scripts/setup-pi.sh` for Postgres + Mosquitto.

Implement nodes against **ESP-NOW v1**, gateways against **MQTT v1**; runtime behavior matches **`Esp32Adapter.normalize/1`**.

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
