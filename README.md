# Alona OS — Firmware

Firmware for **ESP32** sensor nodes deployed on the **alona-os system**. Nodes publish telemetry over **MQTT** to the system broker (Raspberry Pi); **`alona-os-core`** ingests messages and stores measurements for the Phoenix LiveView operator UI.

This repo is intentionally separate from application and host setup:

| Repo | Role |
|------|------|
| **alona-os-firmware** (this repo) | ESP32 node firmware — sensors, connectivity, OTA (planned) |
| **alona-os-core** | Elixir umbrella — Postgres, LiveView UI, `alona_ingest` adapters |
| **alona-os-infra** | Pi bootstrap — PostgreSQL, Mosquitto, env templates, systemd |

## Status

**Early stage.** There is no firmware source in this repository yet. `alona-os-core` implements `Esp32Adapter` and a minimal MQTT subscriber for the configured living-room topic; the **on-device** topic/payload contract should still be treated as **not finalized** until firmware ships.

## Role in the stack

```text
ESP32 node(s)  --MQTT-->  Mosquitto (Pi)  -->  alona_ingest  -->  alona_core  -->  LiveView
                              ^
Cerbo GX (Victron) -----------|
```

Design goals match the rest of the **alona-os system**: long uptime, fault isolation, and low maintenance. Firmware should fail safely, reconnect without manual intervention, and avoid coupling to UI or database details — only the MQTT contract matters on the wire.

## Broker and ingest (target)

| Piece | Location | Notes |
|-------|----------|--------|
| MQTT broker | **alona-os system** (Pi) — see **alona-os-infra** | Default listener **1883** (`mosquitto/alona.conf`) |
| Ingest adapter | `alona-os-core/apps/alona_ingest` | `Esp32Adapter.normalize/1`; MQTT ingest via `Mqtt.Handler` → `TopicRouter` |
| Measurement slugs | `alona-os-core` — `measurement_streams` | Stable slugs; UI reads via `Measurements.streams_for_slugs/1` |
| Devices in DB | `devices` table | Optional `firmware_version`, `last_seen_at` for ops |

Production broker URL on the **alona-os system**: `ALONA_MQTT_HOST` / `ALONA_MQTT_PORT` in `/etc/alona/alona.env` (see **alona-os-infra**). Point nodes at the Pi’s LAN address (or a hostname) on port **1883** unless you add TLS/auth.

Cerbo GX energy data uses Venus MQTT paths and **`VictronAdapter`**; ESP32 nodes are a separate adapter path. Do not mix Victron topic layouts into node firmware without an explicit mapping layer.

## Development (when firmware lands)

Until projects and build instructions are added here, use sibling repos for context:

1. **Local dev** (laptop) — `alona-os-core`: `./setup.sh`, `mix phx.server` (seeds supply demo measurements; MQTT not required for UI work).
2. **MQTT on laptop** — run Mosquitto locally or set `ALONA_MQTT_*` to a reachable broker when testing ingest.
3. **alona-os system** — **alona-os-infra** `scripts/setup-pi.sh` for Postgres + Mosquitto on the Pi host.

When implementing publish logic, align payloads with whatever **`Esp32Adapter`** expects (to be defined alongside topic naming). Power and unit conventions should match backend slugs (e.g. **kW** in DB where ingest converts from watts on the wire).

## Related code (core)

| Module | Purpose |
|--------|---------|
| `AlonaIngest.Adapters.Esp32Adapter` | Normalize node JSON (or binary) payloads → measurement writes |
| `AlonaIngest.Mqtt.TopicRouter` | MVP: configured ESP32 topic(s) → `Esp32Adapter` (Victron path not wired) |
| `AlonaIngest.Mqtt.Client` | Supervised `Tortoise311.Connection` (see `apps/alona_ingest/README.md`) |
| `AlonaCore.Measurements.Device` | Device metadata including `firmware_version` |

## Security

Default **alona-os-infra** Mosquitto config allows anonymous clients on the LAN. That is acceptable on the **alona-os system** LAN only. Before untrusted networks: TLS, credentials, and firewall rules on the Pi broker.

## License

TBD — align with sibling Alona OS repositories when this repo gains substantive content.
