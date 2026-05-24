#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

esp_err_t wifi_ap_start(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = CONFIG_LOADER_AP_CHANNEL,
            .max_connection = 4,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    strlcpy((char *)wifi_config.ap.ssid, CONFIG_LOADER_AP_SSID, sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password, CONFIG_LOADER_AP_PASSWORD, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(CONFIG_LOADER_AP_SSID);

    if (strlen(CONFIG_LOADER_AP_PASSWORD) >= 8) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        wifi_config.ap.password[0] = '\0';
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s channel=%d", CONFIG_LOADER_AP_SSID, CONFIG_LOADER_AP_CHANNEL);
    return ESP_OK;
}
