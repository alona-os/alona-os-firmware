#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** max UTF-8 bytes for device_id from ESP-NOW v1 (fits typical ids with margin) */
#define ALONA_PROTO_DEVICE_ID_MAX 96
/** max measured_at string from node */
#define ALONA_PROTO_MEASURED_AT_MAX 48
/** ESP-NOW v1 recommends staying within this bound */
#define ALONA_PROTO_ESPNOW_V1_MAX_BYTES 250

typedef enum {
    ALONA_PROTO_OK = 0,
    ALONA_PROTO_ERR_TOO_LARGE,
    ALONA_PROTO_ERR_INVALID_JSON,
    ALONA_PROTO_ERR_VERSION,
    ALONA_PROTO_ERR_DEVICE_ID,
    ALONA_PROTO_ERR_READINGS,
    ALONA_PROTO_ERR_NO_MAPPABLE_READINGS,
} alona_proto_err_t;

typedef struct {
    char device_id[ALONA_PROTO_DEVICE_ID_MAX];
    char measured_at[ALONA_PROTO_MEASURED_AT_MAX];

    bool has_temperature_c;
    float temperature_c;

    bool has_relative_humidity_pct;
    float relative_humidity_pct;

    bool has_battery_mv;
    float battery_mv;

    /** true when node frame included numeric rssi_dbm (use instead of recv RSSI when building MQTT) */
    bool has_rssi_dbm_node;
    float rssi_dbm_node;
} alona_espnow_v1_frame_t;

/**
 * Parse ESP-NOW v1 JSON per docs/esp32-espnow-v1.md.
 * Preserves device_id bytes verbatim (no trim/normalize).
 */
alona_proto_err_t alona_espnow_v1_decode(const char *json, size_t len, alona_espnow_v1_frame_t *out);

/**
 * Build MQTT v1 JSON per docs/esp32-mqtt-v1.md.
 * device_id is copied verbatim from `frame`.
 * If node did not send rssi_dbm, uses recv_rssi for top-level rssi_dbm when MQTT includes it.
 * Returns ALONA_PROTO_OK on success; buf holds null-terminated JSON.
 */
alona_proto_err_t alona_mqtt_v1_build(const alona_espnow_v1_frame_t *frame, int8_t recv_rssi,
                                       char *buf, size_t buf_len);

const char *alona_proto_err_str(alona_proto_err_t err);

#ifdef __cplusplus
}
#endif
