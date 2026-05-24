#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool success;
    uint32_t bytes_received;
    uint32_t statements_executed;
    uint32_t sir_commands;
    uint32_t sdr_commands;
    char message[128];
} svf_result_t;

esp_err_t jtag_player_init(void);
esp_err_t svf_player_begin(void);
esp_err_t svf_player_feed(const uint8_t *data, size_t len);
esp_err_t svf_player_finish(svf_result_t *result);
esp_err_t raw_bitstream_player_begin(size_t total_bytes);
esp_err_t raw_bitstream_player_feed(const uint8_t *data, size_t len, bool is_last_chunk);
esp_err_t raw_bitstream_player_finish(void);
void raw_bitstream_player_abort(void);
const char *svf_player_last_error(void);
