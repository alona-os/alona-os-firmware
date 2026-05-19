# ESP32 ESP-NOW wire contract — sensor node → gateway (v1)

Canonical firmware-facing contract for **sensor nodes** talking to a **gateway ESP32** on the LAN. Nodes do **not** publish MQTT to the Pi; the gateway bridges ESP-NOW frames into the MQTT v1 contract in [`esp32-mqtt-v1.md`](esp32-mqtt-v1.md).

Runtime authority for the **gateway → backend** path remains `AlonaIngest.Adapters.Esp32Adapter` in **alona-os-core**.

## Roles

| Role | Connectivity | Responsibility |
|------|--------------|----------------|
| **Sensor node** | ESP-NOW only (no broker) | Sample sensors, send compact telemetry to its gateway peer(s) |
| **Gateway ESP32** | Wi-Fi + MQTT to Pi broker | Receive ESP-NOW frames, normalize, publish [`esp32-mqtt-v1.md`](esp32-mqtt-v1.md) JSON on the configured topic |

Design intent: many low-power nodes, few gateways. Gateways isolate broker credentials and Wi-Fi churn from battery-powered nodes.

## Stack (target)

```text
Sensor node(s)  --ESP-NOW-->  Gateway ESP32  --MQTT JSON v1-->  Mosquitto (Pi)
                                                                    |
                                                         alona_ingest / LiveView
```

Cerbo GX and other vendors still use their own MQTT paths; ESP-NOW is **only** the node ↔ gateway radio layer.

## Living Room MVP

- **Target topology:** living-room sensor node → living-room gateway → topic `alona/esp32/living-room/telemetry`.
- **MVP shortcut:** a single ESP32 may act as **both** node and gateway (local sensor read + MQTT publish). The backend contract is unchanged; `device_id` still identifies the logical node.
- Backend ingest for the MVP is **MQTT-only**; ESP-NOW pairing and forwarding are firmware concerns.

## ESP-NOW payload (v1)

UTF-8 JSON object (max **250 bytes** recommended to stay within ESP-NOW limits with margin):

| Field | Required | Type | Notes |
|-------|----------|------|--------|
| `version` | yes | integer | Must be **1**. |
| `device_id` | yes | string | Non-empty; copied through to MQTT `device_id`. |
| `readings` | yes | object | Same keys/units as MQTT v1 Living Room MVP (below). |
| `measured_at` | no | string | ISO-8601 if present; gateway may set when publishing MQTT. |
| `battery_mv` | no | number | Millivolts; forwarded to MQTT optional fields when numeric. |
| `rssi_dbm` | no | number | **Node-reported** link metric if available; gateway may overwrite with ESP-NOW RSSI when forwarding. |

### Readings map (`readings`)

Same as MQTT v1 Living Room MVP:

| Reading key | Unit | Gateway maps to MQTT slug (via backend adapter) |
|-------------|------|--------------------------------------------------|
| `temperature_c` | degrees Celsius | `env_living_temp_c` |
| `relative_humidity_pct` | percent RH | `env_living_rh` |

Unknown reading keys are ignored at the gateway when building MQTT JSON. Known keys with non-numeric values are skipped.

## Example node frame

```json
{
  "version": 1,
  "device_id": "living-room-node-01",
  "readings": {
    "temperature_c": 22.4,
    "relative_humidity_pct": 58.1
  },
  "battery_mv": 4120
}
```

## Gateway forwarding rules

When publishing to MQTT, the gateway MUST:

1. Set MQTT payload `version` to **1**.
2. Copy `device_id`, `readings`, and optional `measured_at`, `battery_mv`, `rssi_dbm` from the node frame (gateway may add `measured_at` if absent).
3. Publish to the location topic (Living Room: `alona/esp32/living-room/telemetry`).
4. Not merge readings from multiple nodes into one MQTT message — **one node frame → one MQTT publish** (gateway may batch only by sending sequential publishes).

## Pairing and security (firmware)

Not enforced by **alona-os-core** today. Intended firmware behavior:

- Gateway maintains an allowlist of peer MAC addresses (or derived node ids).
- Nodes target the gateway MAC; no broadcast-to-all-broker topology.
- Rotate ESP-NOW PMK/LMK before untrusted RF environments; document keys per property offline.

## Versioning

- **v1** is locked for the Living Room MVP alongside MQTT v1.
- Breaking ESP-NOW layout requires a new `version`, gateway translation, and doc update; backend MQTT v1 can remain if the gateway still emits the same JSON.

## Related

- Gateway → Pi MQTT contract: [`esp32-mqtt-v1.md`](esp32-mqtt-v1.md)
- Backend adapter: `alona-os-core/apps/alona_ingest/lib/alona_ingest/adapters/esp32_adapter.ex`
- Implementation state: workspace `AGENTS.md` §6–§7
