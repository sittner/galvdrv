idf_component_register(
    SRCS
        "main.c"
        "wifi_ap.c"
        "http_server.c"
        "jtag_player.c"
        "siggen_spi.c"
        "siggen_http.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_wifi nvs_flash driver json
)
