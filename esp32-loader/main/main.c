#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "http_server.h"
#include "jtag_player.h"
#include "siggen_spi.h"
#include "wifi_ap.h"

static const char *TAG = "esp32_loader";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(jtag_player_init());
    ESP_ERROR_CHECK(siggen_spi_init());
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_ERROR_CHECK(loader_http_server_start());

    ESP_LOGI(TAG, "ESP32 loader ready. API: /api/bitstream and /api/siggen");
}
