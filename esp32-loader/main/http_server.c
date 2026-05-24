#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "http_server.h"
#include "jtag_player.h"

static const char *TAG = "http_server";

static esp_err_t bitstream_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"empty request body\"}");
    }

    if (req->content_len > CONFIG_LOADER_MAX_UPLOAD_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"SVF exceeds max upload size\"}");
    }

    esp_err_t err = svf_player_begin();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"failed to initialize SVF player\"}");
    }

    char buf[1024];
    int remaining = req->content_len;

    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"failed while receiving upload\"}");
        }

        err = svf_player_feed((const uint8_t *)buf, (size_t)received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SVF feed failed: %s", svf_player_last_error());
            httpd_resp_set_status(req, "400 Bad Request");
            char response[256];
            snprintf(response, sizeof(response),
                     "{\"ok\":false,\"error\":\"%s\"}",
                     svf_player_last_error());
            return httpd_resp_sendstr(req, response);
        }

        remaining -= received;
    }

    svf_result_t result = {0};
    err = svf_player_finish(&result);
    if (err != ESP_OK || !result.success) {
        ESP_LOGE(TAG, "SVF programming failed: %s", svf_player_last_error());
        httpd_resp_set_status(req, "400 Bad Request");
        char response[320];
        snprintf(response, sizeof(response),
                 "{\"ok\":false,\"error\":\"%s\",\"bytes\":%" PRIu32 ",\"statements\":%" PRIu32 "}",
                 result.message[0] ? result.message : svf_player_last_error(),
                 result.bytes_received,
                 result.statements_executed);
        return httpd_resp_sendstr(req, response);
    }

    char response[320];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"bytes\":%" PRIu32 ",\"statements\":%" PRIu32 ",\"sir\":%" PRIu32 ",\"sdr\":%" PRIu32 ",\"done_pin\":null}",
             result.bytes_received,
             result.statements_executed,
             result.sir_commands,
             result.sdr_commands);

    ESP_LOGI(TAG, "SVF upload complete: bytes=%" PRIu32 " statements=%" PRIu32, result.bytes_received, result.statements_executed);
    return httpd_resp_sendstr(req, response);
}

esp_err_t loader_http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_LOADER_HTTP_SERVER_PORT;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t upload_uri = {
        .uri = "/api/bitstream",
        .method = HTTP_POST,
        .handler = bitstream_post_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &upload_uri));
    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}
