# ESP32 MQTT wire contract — gateway → Pi (Living Room MVP v1)

Canonical contract for the **gateway ESP32 → Mosquitto → alona-os-core** hop. Sensor nodes reach the gateway over **ESP-NOW** per [`esp32-espnow-v1.md`](esp32-espnow-v1.md); only the gateway publishes this MQTT JSON.

Aligned with `AlonaIngest.Adapters.Esp32Adapter` in the umbrella (runtime authority for ingest).

## Versioning

- **v1** is **locked** for the Living Room MVP: payload `version` must be integer **1**.
- Breaking wire changes require a new **`version`** (e.g. `2`), a new adapter/doc pair, and coordinated firmware + backend rollout.

## Topic

Publish UTF-8 JSON to:

```text
alona/esp32/living-room/telemetry
```

The ingest subscriber uses this literal topic by default (`config/config.exs`, `mqtt_runtime.exs`). If operators override `ALONA_MQTT_TOPICS`, this topic must remain in the comma-separated list or messages will not be ingested.

QoS is **0** on the backend subscription; firmware may publish at QoS 0 unless you standardize otherwise.

## JSON payload (v1)

Top-level object:

| Field | Required | Type | Notes |
|-------|----------|------|--------|
| `version` | yes | integer | Must be **1**. |
| `device_id` | yes | string | Non-empty; stored in envelope `raw` for provenance only (no per-device routing yet). |
| `readings` | yes | object | Map of reading keys to values (see below). |
| `measured_at` | no | string | Non-empty ISO-8601 timestamp if present; omitted or empty → ingest uses envelope default time. |
| `battery_mv` | no | number | Millivolts; optional metadata in `raw` (numeric only). |
| `rssi_dbm` | no | number | dBm; optional metadata in `raw` (numeric only). |

### Readings map (`readings`)

Supported keys (Living Room MVP):

| Reading key | Unit | Backend stream slug |
|-------------|------|----------------------|
| `temperature_c` | degrees Celsius | `env_living_temp_c` |
| `relative_humidity_pct` | percent RH (0–100 scale) | `env_living_rh` |

Behavior:

- **Unknown keys** are **ignored** (no error).
- **Known keys with non-numeric values** are **skipped** (other known numeric readings still ingest).
- If, after filtering, **no** mappable numeric readings remain, the adapter returns **`no_mappable_readings`** (see errors).

At least one numeric value for a supported key must be present for a successful normalize.

### Property and streams

Ingest resolves streams under the default property **`default-site`** unless the internal envelope adds `property_slug` (the ESP32 adapter does not set it).

**Prerequisite:** `measurement_streams` rows for `env_living_temp_c` and `env_living_rh` must exist (e.g. seeds / migrations). If one stream is missing, points that map to the missing slug fail ingest while others may still persist (**partial ingest**).

## Example payloads

Full message:

```json
{
  "version": 1,
  "device_id": "living-room-node-01",
  "measured_at": "2026-05-19T19:30:00Z",
  "readings": {
    "temperature_c": 22.4,
    "relative_humidity_pct": 58.1
  },
  "battery_mv": 4120,
  "rssi_dbm": -67
}
```

Humidity-only:

```json
{
  "version": 1,
  "device_id": "living-room-node-01",
  "readings": {
    "relative_humidity_pct": 58.1
  }
}
```

## Errors (adapter vs router)

| Condition | Adapter `normalize/1` | `TopicRouter.route/2` (configured topic) |
|-----------|----------------------|------------------------------------------|
| Wrong `version` | `unsupported_version` | `{:error, :unsupported_version}` |
| Missing / invalid `device_id` or `readings` | `invalid_payload` | `{:error, :invalid_payload}` |
| Invalid JSON bytes | `invalid_json` | `{:error, :invalid_json}` |
| No mappable numeric readings | `no_mappable_readings` | `{:error, :no_mappable_readings}` |
| Topic not in configured list | (not called) | `{:ok, :ignored}` |
| One envelope succeeds, another fails DB lookup | — | `{:error, {:partial_ingest_failed, [{slug, reason}, ...]}}` |

Successful full ingest returns `{:ok, :ingested}`.

## Related

- Node → gateway (ESP-NOW): [`esp32-espnow-v1.md`](esp32-espnow-v1.md)
- Operator summary and broker env vars: `alona-os-core/apps/alona_ingest/README.md`
- Adapter implementation: `alona-os-core/apps/alona_ingest/lib/alona_ingest/adapters/esp32_adapter.ex`
