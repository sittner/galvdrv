#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SIGGEN_SAMPLE_RATE_HZ 125000.0
#define SIGGEN_MAX_FREQ_HZ (SIGGEN_SAMPLE_RATE_HZ / 2.0)

#define SCOPE_SAMPLES_PER_CH 1280
#define SCOPE_NUM_CHANNELS   4

typedef enum {
    SIGGEN_WAVE_SINE = 0,
    SIGGEN_WAVE_SQUARE = 1,
    SIGGEN_WAVE_RAMP = 2,
    SIGGEN_WAVE_TRIANGLE = 3,
} siggen_waveform_t;

typedef enum {
    SCOPE_ST_IDLE = 0,
    SCOPE_ST_ARMED = 1,
    SCOPE_ST_TRIGGERED = 2,
    SCOPE_ST_DONE = 3,
} scope_state_t;

typedef enum {
    SCOPE_TRIG_SINGLE = 0,
    SCOPE_TRIG_NORMAL = 1,
    SCOPE_TRIG_MANUAL = 2,
} scope_trig_mode_t;

esp_err_t siggen_spi_init(void);
esp_err_t siggen_write_reg(uint8_t addr, uint16_t value);
esp_err_t siggen_read_reg(uint8_t addr, uint16_t *value_out);
esp_err_t siggen_set_freq(uint8_t channel, float freq_hz);
esp_err_t siggen_set_waveform(uint8_t channel, uint8_t waveform);
esp_err_t siggen_set_amplitude(uint8_t channel, uint16_t amplitude);
esp_err_t siggen_set_duty(uint8_t channel, uint16_t duty);
esp_err_t siggen_enable(uint8_t channel, bool enable);

// Scope API
esp_err_t scope_configure(uint8_t trig_ch, bool trig_falling, uint8_t trig_mode, uint16_t sample_div);
esp_err_t scope_arm(void);
esp_err_t scope_force_trigger(void);
esp_err_t scope_get_status(uint8_t *state_out, uint16_t *trig_ptr_out);
esp_err_t scope_read_buffer(uint16_t start_addr, uint16_t *buf, uint16_t count);
