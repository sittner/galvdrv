#include <math.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "siggen_spi.h"

static const char *TAG = "siggen_spi";

static spi_device_handle_t s_siggen_dev;

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
        (uint8_t)(addr & 0x7Fu),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    spi_transaction_t trans = {
        .length = 24,
        .tx_buffer = frame,
    };

    return spi_device_polling_transmit(s_siggen_dev, &trans);
}

esp_err_t siggen_read_reg(uint8_t addr, uint16_t *value_out)
{
    if (!s_siggen_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!value_out) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_frame[3] = {
        (uint8_t)(0x80u | (addr & 0x7Fu)),
        0,
        0,
    };
    uint8_t rx_frame[3] = {0};

    spi_transaction_t trans = {
        .length = 24,
        .tx_buffer = tx_frame,
        .rx_buffer = rx_frame,
    };

    esp_err_t err = spi_device_polling_transmit(s_siggen_dev, &trans);
    if (err != ESP_OK) {
        return err;
    }

    *value_out = (uint16_t)(((uint16_t)rx_frame[1] << 8) | rx_frame[2]);
    return ESP_OK;
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

    uint16_t enable_mask = 0;
    ESP_RETURN_ON_ERROR(siggen_read_reg(0x10, &enable_mask), TAG, "read enable mask failed");

    if (enable) {
        enable_mask |= (uint16_t)(1u << channel);
    } else {
        enable_mask &= (uint16_t)~(1u << channel);
    }

    return siggen_write_reg(0x10, enable_mask);
}

// --- Scope API ---

esp_err_t scope_configure(uint8_t trig_ch, bool trig_falling, uint8_t trig_mode, uint16_t sample_div)
{
    uint16_t cfg = (uint16_t)(((trig_ch & 0x3) << 3) | ((trig_falling ? 1 : 0) << 2) | (trig_mode & 0x3));
    ESP_RETURN_ON_ERROR(siggen_write_reg(0x20, cfg), TAG, "scope config failed");
    ESP_RETURN_ON_ERROR(siggen_write_reg(0x21, sample_div), TAG, "scope sample_div failed");
    return ESP_OK;
}

esp_err_t scope_arm(void)
{
    return siggen_write_reg(0x23, 0x0001);
}

esp_err_t scope_force_trigger(void)
{
    return siggen_write_reg(0x23, 0x0002);
}

esp_err_t scope_get_status(uint8_t *state_out, uint16_t *trig_ptr_out)
{
    uint16_t status = 0;
    ESP_RETURN_ON_ERROR(siggen_read_reg(0x23, &status), TAG, "scope status read failed");
    if (state_out) {
        *state_out = (uint8_t)(status & 0x3);
    }
    if (trig_ptr_out) {
        uint16_t ptr = 0;
        ESP_RETURN_ON_ERROR(siggen_read_reg(0x22, &ptr), TAG, "scope trig_ptr read failed");
        *trig_ptr_out = ptr;
    }
    return ESP_OK;
}

esp_err_t scope_read_buffer(uint16_t start_addr, uint16_t *buf, uint16_t count)
{
    // Set read address
    ESP_RETURN_ON_ERROR(siggen_write_reg(0x22, start_addr), TAG, "scope set rd_addr failed");

    // Read samples sequentially (auto-increment)
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t val = 0;
        ESP_RETURN_ON_ERROR(siggen_read_reg(0x24, &val), TAG, "scope buffer read failed");
        buf[i] = val;
    }
    return ESP_OK;
}
