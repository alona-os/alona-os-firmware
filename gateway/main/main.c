/*
 * ESP-NOW → MQTT gateway for Alona OS Living Room MVP (ESP-IDF).
 * See docs/gateway-setup.md and docs/esp32-*.md in alona-os-firmware.
 */

#include "alona_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alona_protocol.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

static const char *TAG_GW = "alona_gw";
static const char *TAG_WIFI = "alona_wifi";
static const char *TAG_MQTT = "alona_mqtt";
static const char *TAG_ESPNOW = "alona_espnow";

#ifndef ALONA_ESPNOW_RX_QUEUE_DEPTH
#define ALONA_ESPNOW_RX_QUEUE_DEPTH 12
#endif

#ifndef ALONA_DEDUPE_RING_CAP
#define ALONA_DEDUPE_RING_CAP 8
#endif

typedef struct {
    uint8_t data[ALONA_PROTO_ESPNOW_V1_MAX_BYTES];
    size_t len;
    int8_t rssi;
    uint8_t peer[ESP_NOW_ETH_ALEN];
} espnow_rx_item_t;

static QueueHandle_t s_rx_queue;
static esp_mqtt_client_handle_t s_mqtt_client;
static volatile bool s_mqtt_connected;
static volatile bool s_wifi_got_ip;
static volatile bool s_mqtt_started;

typedef struct {
    bool used;
    char device_id[ALONA_PROTO_DEVICE_ID_MAX];
    char measured_at[ALONA_PROTO_MEASURED_AT_MAX];
    int64_t last_seen_us;
} dedupe_slot_t;

static dedupe_slot_t s_dedupe[ALONA_DEDUPE_RING_CAP];

static void log_mac(const char *prefix, const uint8_t mac[ESP_NOW_ETH_ALEN]) {
    ESP_LOGI(TAG_GW, "%s %02x:%02x:%02x:%02x:%02x:%02x", prefix, mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
}

static void log_wifi_channel(void) {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        ESP_LOGI(TAG_WIFI, "wifi channel=%u (fake node must match this channel)", primary);
    } else {
        ESP_LOGW(TAG_WIFI, "esp_wifi_get_channel failed");
    }
}

/** returns true if this frame should be dropped as a duplicate */
static bool dedupe_should_drop(const alona_espnow_v1_frame_t *frame) {
    int64_t now = esp_timer_get_time();
    int64_t window_us = (int64_t)ALONA_DEDUPE_WINDOW_MS * 1000LL;
    const char *meas = frame->measured_at;

    int empty_idx = -1;
    for (int i = 0; i < ALONA_DEDUPE_RING_CAP; i++) {
        if (!s_dedupe[i].used) {
            if (empty_idx < 0) {
                empty_idx = i;
            }
            continue;
        }
        if (strcmp(s_dedupe[i].device_id, frame->device_id) != 0) {
            continue;
        }
        if (strcmp(s_dedupe[i].measured_at, meas) != 0) {
            continue;
        }
        if ((now - s_dedupe[i].last_seen_us) < window_us) {
            return true;
        }
        s_dedupe[i].last_seen_us = now;
        return false;
    }

    int slot = empty_idx;
    if (slot < 0) {
        slot = 0;
        int64_t oldest = s_dedupe[0].last_seen_us;
        for (int i = 1; i < ALONA_DEDUPE_RING_CAP; i++) {
            if (s_dedupe[i].last_seen_us < oldest) {
                oldest = s_dedupe[i].last_seen_us;
                slot = i;
            }
        }
    }

    s_dedupe[slot].used = true;
    strncpy(s_dedupe[slot].device_id, frame->device_id, sizeof(s_dedupe[slot].device_id) - 1);
    s_dedupe[slot].device_id[sizeof(s_dedupe[slot].device_id) - 1] = '\0';
    strncpy(s_dedupe[slot].measured_at, meas, sizeof(s_dedupe[slot].measured_at) - 1);
    s_dedupe[slot].measured_at[sizeof(s_dedupe[slot].measured_at) - 1] = '\0';
    s_dedupe[slot].last_seen_us = now;
    return false;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    (void)handler_args;
    (void)event_data;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "mqtt connected");
        s_mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG_MQTT, "mqtt disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG_MQTT, "mqtt error");
        break;
    default:
        break;
    }
}

static char s_mqtt_uri[192];

static void start_mqtt_once(void) {
    if (s_mqtt_started) {
        return;
    }
    s_mqtt_started = true;

    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtt://" ALONA_MQTT_HOST ":%d", ALONA_MQTT_PORT);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_mqtt_uri,
    };

    s_mqtt_client = esp_mqtt_client_init(&cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG_MQTT, "esp_mqtt_client_init failed");
        s_mqtt_started = false;
        return;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MQTT, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_mqtt_started = false;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_WIFI, "wifi sta start, connecting");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG_WIFI, "wifi disconnected, reconnecting");
        s_wifi_got_ip = false;
        s_mqtt_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG_WIFI, "wifi connected (got ip)");
        s_wifi_got_ip = true;
        log_wifi_channel();
        start_mqtt_once();
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (data == NULL || len <= 0) {
        return;
    }
    if (len > (int)ALONA_PROTO_ESPNOW_V1_MAX_BYTES) {
        ESP_LOGW(TAG_ESPNOW, "frame too large: %d", len);
        return;
    }

    espnow_rx_item_t item;
    memset(&item, 0, sizeof(item));
    item.len = (size_t)len;
    memcpy(item.data, data, item.len);

    if (recv_info != NULL) {
        memcpy(item.peer, recv_info->src_addr, ESP_NOW_ETH_ALEN);
        if (recv_info->rx_ctrl != NULL) {
            item.rssi = recv_info->rx_ctrl->rssi;
        }
    }

    ESP_LOGI(TAG_ESPNOW, "esp-now rx len=%u rssi=%d", (unsigned)item.len, (int)item.rssi);
    log_mac("peer", item.peer);

    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG_ESPNOW, "rx queue full, dropping frame");
    }
}

static esp_err_t wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ALONA_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, ALONA_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    uint8_t mac[ESP_NOW_ETH_ALEN];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        log_mac("gateway sta mac", mac);
    }

    return ESP_OK;
}

static esp_err_t espnow_init(void) {
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "esp_now_init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_LOGI(TAG_ESPNOW, "esp-now ready (recv)");
    return ESP_OK;
}

static void mqtt_worker_task(void *pv) {
    (void)pv;
    char mqtt_buf[512];

    for (;;) {
        espnow_rx_item_t item;
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        alona_espnow_v1_frame_t frame;
        alona_proto_err_t dec =
            alona_espnow_v1_decode((const char *)item.data, item.len, &frame);
        if (dec != ALONA_PROTO_OK) {
            ESP_LOGW(TAG_ESPNOW, "decode failed: %s", alona_proto_err_str(dec));
            continue;
        }

        ESP_LOGI(TAG_ESPNOW, "decoded device_id=%s temp=%s rh=%s", frame.device_id,
                 frame.has_temperature_c ? "yes" : "no", frame.has_relative_humidity_pct ? "yes" : "no");

        if (dedupe_should_drop(&frame)) {
            ESP_LOGI(TAG_ESPNOW, "duplicate frame dropped (same device_id + measured_at within window)");
            continue;
        }

        dec = alona_mqtt_v1_build(&frame, item.rssi, mqtt_buf, sizeof(mqtt_buf));
        if (dec != ALONA_PROTO_OK) {
            ESP_LOGW(TAG_MQTT, "mqtt build failed: %s", alona_proto_err_str(dec));
            continue;
        }

        ESP_LOGI(TAG_MQTT, "mqtt payload (trunc): %.220s", mqtt_buf);

        if (!s_mqtt_connected || s_mqtt_client == NULL) {
            ESP_LOGW(TAG_MQTT, "mqtt not connected, skip publish");
            continue;
        }

        int mid =
            esp_mqtt_client_publish(s_mqtt_client, ALONA_MQTT_TOPIC, mqtt_buf, 0, 0, 0);
        if (mid < 0) {
            ESP_LOGW(TAG_MQTT, "mqtt publish failed mid=%d", mid);
        } else {
            ESP_LOGI(TAG_MQTT, "mqtt publish ok msg_id=%d", mid);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG_GW, "boot gateway id=%s (label only; mqtt uses node device_id verbatim)",
             ALONA_GATEWAY_DEVICE_ID);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    memset(s_dedupe, 0, sizeof(s_dedupe));

    s_rx_queue = xQueueCreate(ALONA_ESPNOW_RX_QUEUE_DEPTH, sizeof(espnow_rx_item_t));
    if (s_rx_queue == NULL) {
        ESP_LOGE(TAG_GW, "failed to create rx queue");
        return;
    }

    BaseType_t ok =
        xTaskCreatePinnedToCore(mqtt_worker_task, "alona_mqtt_worker", 8192, NULL, 5, NULL, tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGE(TAG_GW, "failed to start mqtt worker task");
        return;
    }

    ESP_ERROR_CHECK(wifi_init_sta());

    /* ESP-NOW after Wi-Fi started; channel follows AP once associated */
    while (!s_wifi_got_ip) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_ERROR_CHECK(espnow_init());
}
