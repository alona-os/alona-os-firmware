# Property manifest — single source for firmware Wi‑Fi / MQTT / ESP‑NOW values

Operator-owned **`property.local.json`** (gitignored) drives:

- [`gateway/main/alona_config.h`](../gateway/main/alona_config.h.example) — Wi‑Fi STA + MQTT + gateway tuning  
- [`examples/temp_humidity_node/main/alona_config.h`](../examples/temp_humidity_node/main/alona_config.h.example) — channel + gateway STA MAC + node id  
- [`examples/espnow_fake_node/main/alona_config.h`](../examples/espnow_fake_node/main/alona_config.h.example) — same radio fields + bench device id  

It does **not** replace Pi secrets (`SECRET_KEY_BASE`, DB passwords): those stay in **`alona-os-infra`** [`env/alona.env.example`](../../../alona-os-infra/env/alona.env.example) when repos are cloned side-by-side with this workspace. The sync script prints **`ALONA_MQTT_*`** exports so MQTT host/port/topic stay aligned with the gateway firmware.

## Workflow

From **`alona-os-firmware/`** repo root:

```bash
cp property.local.json.example property.local.json
# edit property.local.json — real wifi, mqtt host (LAN IP of Mosquitto), gateway_sta_mac + wifi_channel from gateway serial
python3 scripts/sync_property_manifest.py
```

Then build/flash each IDF project as usual (`gateway`, `examples/temp_humidity_node`, …).

Options:

```bash
python3 scripts/sync_property_manifest.py --manifest /path/to/property.local.json
python3 scripts/sync_property_manifest.py --dry-run
```

## Fields

| Section | Purpose |
|--------|---------|
| `wifi` | Gateway STA only (`ssid`, `password`). Nodes do not join Wi‑Fi. |
| `mqtt` | Broker **`host`** (LAN IPv4 — never `127.0.0.1` on the gateway ESP32), **`port`**, **`living_room_topic`**. |
| `gateway` | Optional log label + dedupe / RX queue defaults. |
| `espnow` | **`wifi_channel`** (must match gateway AP channel after association), **`gateway_sta_mac`** (`aa:bb:…` from gateway log). Shared by all ESP‑NOW sender examples. |
| `nodes.temp_humidity` | `device_id`, `send_interval_ms`. |
| `nodes.fake_bench` | Same for [`examples/espnow_fake_node`](../examples/espnow_fake_node/). |

After editing **`gateway_sta_mac`** or **`wifi_channel`**, re-run the script before rebuilding node firmware.
