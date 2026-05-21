#include "alona_protocol.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

static bool copy_json_string_field(cJSON *root, const char *key, char *dst, size_t dst_cap) {
    cJSON *j = cJSON_GetObjectItem(root, key);
    if (j == NULL || !cJSON_IsString(j)) {
        return false;
    }
    const char *s = cJSON_GetStringValue(j);
    if (s == NULL) {
        return false;
    }
    size_t n = strnlen(s, dst_cap);
    if (n >= dst_cap) {
        return false;
    }
    memcpy(dst, s, n + 1);
    return true;
}

static bool require_version_1(cJSON *root) {
    cJSON *j = cJSON_GetObjectItem(root, "version");
    return j != NULL && cJSON_IsNumber(j) && j->valuedouble == 1.0;
}

static bool copy_numeric_reading(cJSON *readings, const char *key, bool *has_out, float *val_out) {
    *has_out = false;
    cJSON *j = cJSON_GetObjectItem(readings, key);
    if (j == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(j)) {
        return true;
    }
    double v = j->valuedouble;
    if (!isfinite(v)) {
        return true;
    }
    *val_out = (float)v;
    *has_out = true;
    return true;
}

static bool copy_optional_number_top(cJSON *root, const char *key, bool *has_out, float *val_out) {
    *has_out = false;
    cJSON *j = cJSON_GetObjectItem(root, key);
    if (j == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(j)) {
        return true;
    }
    double v = j->valuedouble;
    if (!isfinite(v)) {
        return true;
    }
    *val_out = (float)v;
    *has_out = true;
    return true;
}

alona_proto_err_t alona_espnow_v1_decode(const char *json, size_t len, alona_espnow_v1_frame_t *out) {
    if (json == NULL || out == NULL) {
        return ALONA_PROTO_ERR_INVALID_JSON;
    }
    if (len > ALONA_PROTO_ESPNOW_V1_MAX_BYTES) {
        return ALONA_PROTO_ERR_TOO_LARGE;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    alona_proto_err_t err = ALONA_PROTO_OK;

    if (!require_version_1(root)) {
        err = ALONA_PROTO_ERR_VERSION;
        goto cleanup;
    }

    if (!copy_json_string_field(root, "device_id", out->device_id, sizeof(out->device_id))) {
        err = ALONA_PROTO_ERR_DEVICE_ID;
        goto cleanup;
    }
    if (out->device_id[0] == '\0') {
        err = ALONA_PROTO_ERR_DEVICE_ID;
        goto cleanup;
    }

    cJSON *readings = cJSON_GetObjectItem(root, "readings");
    if (readings == NULL || !cJSON_IsObject(readings)) {
        err = ALONA_PROTO_ERR_READINGS;
        goto cleanup;
    }

    (void)copy_numeric_reading(readings, "temperature_c", &out->has_temperature_c, &out->temperature_c);
    (void)copy_numeric_reading(readings, "relative_humidity_pct", &out->has_relative_humidity_pct,
                               &out->relative_humidity_pct);

    if (!out->has_temperature_c && !out->has_relative_humidity_pct) {
        err = ALONA_PROTO_ERR_NO_MAPPABLE_READINGS;
        goto cleanup;
    }

    (void)copy_json_string_field(root, "measured_at", out->measured_at, sizeof(out->measured_at));

    (void)copy_optional_number_top(root, "battery_mv", &out->has_battery_mv, &out->battery_mv);
    (void)copy_optional_number_top(root, "rssi_dbm", &out->has_rssi_dbm_node, &out->rssi_dbm_node);

cleanup:
    cJSON_Delete(root);
    return err;
}

static bool append_number_field(cJSON *obj, const char *key, float v) {
    cJSON *n = cJSON_CreateNumber((double)v);
    if (n == NULL) {
        return false;
    }
    cJSON_AddItemToObject(obj, key, n);
    return true;
}

alona_proto_err_t alona_mqtt_v1_build(const alona_espnow_v1_frame_t *frame, int8_t recv_rssi,
                                      char *buf, size_t buf_len) {
    if (frame == NULL || buf == NULL || buf_len < 64) {
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    if (!frame->has_temperature_c && !frame->has_relative_humidity_pct) {
        return ALONA_PROTO_ERR_NO_MAPPABLE_READINGS;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "device_id", frame->device_id);

    cJSON *readings = cJSON_CreateObject();
    if (readings == NULL) {
        cJSON_Delete(root);
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    if (frame->has_temperature_c && !append_number_field(readings, "temperature_c", frame->temperature_c)) {
        cJSON_Delete(readings);
        cJSON_Delete(root);
        return ALONA_PROTO_ERR_INVALID_JSON;
    }
    if (frame->has_relative_humidity_pct &&
        !append_number_field(readings, "relative_humidity_pct", frame->relative_humidity_pct)) {
        cJSON_Delete(readings);
        cJSON_Delete(root);
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    cJSON_AddItemToObject(root, "readings", readings);

    if (frame->measured_at[0] != '\0') {
        cJSON_AddStringToObject(root, "measured_at", frame->measured_at);
    }

    if (frame->has_battery_mv) {
        cJSON_AddNumberToObject(root, "battery_mv", (double)frame->battery_mv);
    }

    if (frame->has_rssi_dbm_node) {
        cJSON_AddNumberToObject(root, "rssi_dbm", (double)frame->rssi_dbm_node);
    } else {
        cJSON_AddNumberToObject(root, "rssi_dbm", (double)recv_rssi);
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == NULL) {
        return ALONA_PROTO_ERR_INVALID_JSON;
    }

    size_t plen = strlen(printed);
    if (plen + 1 > buf_len) {
        cJSON_free(printed);
        return ALONA_PROTO_ERR_TOO_LARGE;
    }
    memcpy(buf, printed, plen + 1);
    cJSON_free(printed);
    return ALONA_PROTO_OK;
}

const char *alona_proto_err_str(alona_proto_err_t err) {
    switch (err) {
    case ALONA_PROTO_OK:
        return "ok";
    case ALONA_PROTO_ERR_TOO_LARGE:
        return "too_large";
    case ALONA_PROTO_ERR_INVALID_JSON:
        return "invalid_json";
    case ALONA_PROTO_ERR_VERSION:
        return "unsupported_version";
    case ALONA_PROTO_ERR_DEVICE_ID:
        return "invalid_device_id";
    case ALONA_PROTO_ERR_READINGS:
        return "invalid_readings";
    case ALONA_PROTO_ERR_NO_MAPPABLE_READINGS:
        return "no_mappable_readings";
    default:
        return "unknown";
    }
}
