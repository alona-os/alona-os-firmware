/*
 * Living Room temp/RH ESP-NOW node — channel-only Wi-Fi (no association).
 * Payload built via alona_protocol alona_espnow_v1_build().
 */
#include "alona_config.h"
#include "sensor.h"

#include <string.h>

#include "alona_protocol.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "alona_node";

static const uint8_t k_gateway_mac[ESP_NOW_ETH_ALEN] = {
    ALONA_GATEWAY_PEER_MAC_B0,
    ALONA_GATEWAY_PEER_MAC_B1,
    ALONA_GATEWAY_PEER_MAC_B2,
    ALONA_GATEWAY_PEER_MAC_B3,
    ALONA_GATEWAY_PEER_MAC_B4,
    ALONA_GATEWAY_PEER_MAC_B5,
};

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (tx_info == NULL) {
        return;
    }
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "esp-now send ok to %02x:%02x:%02x:%02x:%02x:%02x", tx_info->des_addr[0],
                 tx_info->des_addr[1], tx_info->des_addr[2], tx_info->des_addr[3], tx_info->des_addr[4],
                 tx_info->des_addr[5]);
    } else {
        ESP_LOGW(TAG, "esp-now send failed");
    }
}

static esp_err_t wifi_init_channel_only(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_channel(ALONA_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    uint8_t mac[ESP_NOW_ETH_ALEN];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "node sta mac %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
                 mac[4], mac[5]);
    }

    ESP_LOGI(TAG, "wifi channel fixed to %u (no association)", (unsigned)ALONA_WIFI_CHANNEL);
    return ESP_OK;
}

static esp_err_t espnow_add_gateway_peer(void) {
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, k_gateway_mac, ESP_NOW_ETH_ALEN);
    peer.channel = ALONA_WIFI_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "esp-now peer added (gateway)");
    return ESP_OK;
}

static void send_loop_task(void *pv) {
    (void)pv;
    char payload[ALONA_PROTO_ESPNOW_V1_MAX_BYTES + 1];

    for (;;) {
        temp_humidity_reading_t reading;
        if (!read_temperature_humidity(&reading)) {
            ESP_LOGW(TAG, "read_temperature_humidity failed");
            vTaskDelay(pdMS_TO_TICKS(ALONA_ESPNOW_SEND_INTERVAL_MS));
            continue;
        }

        alona_espnow_v1_frame_t frame = {0};
        strncpy(frame.device_id, ALONA_DEVICE_ID, sizeof(frame.device_id) - 1);
        frame.device_id[sizeof(frame.device_id) - 1] = '\0';
        frame.has_temperature_c = true;
        frame.temperature_c = reading.temperature_c;
        frame.has_relative_humidity_pct = true;
        frame.relative_humidity_pct = reading.relative_humidity_pct;

        alona_proto_err_t build_err =
            alona_espnow_v1_build(&frame, payload, sizeof(payload));
        if (build_err != ALONA_PROTO_OK) {
            ESP_LOGE(TAG, "alona_espnow_v1_build failed: %s", alona_proto_err_str(build_err));
            vTaskDelay(pdMS_TO_TICKS(ALONA_ESPNOW_SEND_INTERVAL_MS));
            continue;
        }

        size_t len = strlen(payload);
        ESP_LOGI(TAG, "sending esp-now json len=%u", (unsigned)len);

        esp_err_t err = esp_now_send(k_gateway_mac, (const uint8_t *)payload, len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_send err=%s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(ALONA_ESPNOW_SEND_INTERVAL_MS));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "boot temp/humidity esp-now node (channel-only, no Wi-Fi join)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(wifi_init_channel_only());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(espnow_add_gateway_peer());

    xTaskCreate(send_loop_task, "alona_node_send", 4096, NULL, 5, NULL);
}
