#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t jtag_player_init(void);
esp_err_t raw_bitstream_player_begin(size_t total_bytes);
esp_err_t raw_bitstream_player_feed(const uint8_t *data, size_t len, bool is_last_chunk);
esp_err_t raw_bitstream_player_finish(void);
void raw_bitstream_player_abort(void);
const char *jtag_player_last_error(void);
