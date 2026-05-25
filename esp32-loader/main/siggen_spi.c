#include <math.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "siggen_spi.h"

static const char *TAG = "siggen_spi";

static spi_device_handle_t s_siggen_dev;
static uint16_t s_enable_mask;

static inline bool channel_valid(uint8_t channel)
{
    return channel < 2;
}

static inline uint8_t channel_base(uint8_t channel)
{
    return channel == 0 ? 0x00 : 0x08;
}

esp_err_t siggen_spi_init(void)
{
    if (s_siggen_dev) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SIGGEN_SPI_GPIO_MOSI,
        .miso_io_num = CONFIG_SIGGEN_SPI_GPIO_MISO,
        .sclk_io_num = CONFIG_SIGGEN_SPI_GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED), TAG, "spi bus init failed");

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CONFIG_SIGGEN_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_SIGGEN_SPI_GPIO_CS,
        .queue_size = 1,
    };

    esp_err_t err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_siggen_dev);
    if (err != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        return err;
    }

    s_enable_mask = 0;
    ESP_LOGI(TAG, "SIGGEN SPI ready on SCLK=%d MOSI=%d MISO=%d CS=%d @ %d Hz",
             CONFIG_SIGGEN_SPI_GPIO_SCLK,
             CONFIG_SIGGEN_SPI_GPIO_MOSI,
             CONFIG_SIGGEN_SPI_GPIO_MISO,
             CONFIG_SIGGEN_SPI_GPIO_CS,
             CONFIG_SIGGEN_SPI_CLOCK_HZ);

    return ESP_OK;
}

esp_err_t siggen_write_reg(uint8_t addr, uint16_t value)
{
    if (!s_siggen_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t frame[3] = {
        addr,
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    spi_transaction_t trans = {
        .length = 24,
        .tx_buffer = frame,
    };

    return spi_device_polling_transmit(s_siggen_dev, &trans);
}

esp_err_t siggen_set_freq(uint8_t channel, float freq_hz)
{
    if (!channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (freq_hz < 0.0f) {
        freq_hz = 0.0f;
    }
    if (freq_hz > (float)SIGGEN_MAX_FREQ_HZ) {
        freq_hz = (float)SIGGEN_MAX_FREQ_HZ;
    }

    uint32_t phase_inc = (uint32_t)llround((double)freq_hz * (4294967296.0 / SIGGEN_SAMPLE_RATE_HZ));
    uint8_t base = channel_base(channel);

    ESP_RETURN_ON_ERROR(siggen_write_reg(base + 0x00, (uint16_t)(phase_inc & 0xFFFFu)), TAG, "set freq low failed");
    ESP_RETURN_ON_ERROR(siggen_write_reg(base + 0x01, (uint16_t)(phase_inc >> 16)), TAG, "set freq high failed");
    return ESP_OK;
}

esp_err_t siggen_set_waveform(uint8_t channel, uint8_t waveform)
{
    if (!channel_valid(channel) || waveform > SIGGEN_WAVE_TRIANGLE) {
        return ESP_ERR_INVALID_ARG;
    }

    return siggen_write_reg(channel_base(channel) + 0x02, waveform);
}

esp_err_t siggen_set_amplitude(uint8_t channel, uint16_t amplitude)
{
    if (!channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    return siggen_write_reg(channel_base(channel) + 0x03, amplitude);
}

esp_err_t siggen_set_duty(uint8_t channel, uint16_t duty)
{
    if (!channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    return siggen_write_reg(channel_base(channel) + 0x04, duty);
}

esp_err_t siggen_enable(uint8_t channel, bool enable)
{
    if (!channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (enable) {
        s_enable_mask |= (uint16_t)(1u << channel);
    } else {
        s_enable_mask &= (uint16_t)~(1u << channel);
    }

    return siggen_write_reg(0x10, s_enable_mask);
}
