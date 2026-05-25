#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SIGGEN_SAMPLE_RATE_HZ 125000.0
#define SIGGEN_MAX_FREQ_HZ (SIGGEN_SAMPLE_RATE_HZ / 2.0)

typedef enum {
    SIGGEN_WAVE_SINE = 0,
    SIGGEN_WAVE_SQUARE = 1,
    SIGGEN_WAVE_RAMP = 2,
    SIGGEN_WAVE_TRIANGLE = 3,
} siggen_waveform_t;

esp_err_t siggen_spi_init(void);
esp_err_t siggen_write_reg(uint8_t addr, uint16_t value);
esp_err_t siggen_set_freq(uint8_t channel, float freq_hz);
esp_err_t siggen_set_waveform(uint8_t channel, uint8_t waveform);
esp_err_t siggen_set_amplitude(uint8_t channel, uint16_t amplitude);
esp_err_t siggen_set_duty(uint8_t channel, uint16_t duty);
esp_err_t siggen_enable(uint8_t channel, bool enable);
