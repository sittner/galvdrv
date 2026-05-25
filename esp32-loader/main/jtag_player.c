#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "jtag_player.h"

typedef enum {
    JTAG_STATE_INVALID = -1,
    JTAG_TEST_LOGIC_RESET = 0,
    JTAG_RUN_TEST_IDLE,
    JTAG_SELECT_DR_SCAN,
    JTAG_CAPTURE_DR,
    JTAG_SHIFT_DR,
    JTAG_EXIT1_DR,
    JTAG_PAUSE_DR,
    JTAG_EXIT2_DR,
    JTAG_UPDATE_DR,
    JTAG_SELECT_IR_SCAN,
    JTAG_CAPTURE_IR,
    JTAG_SHIFT_IR,
    JTAG_EXIT1_IR,
    JTAG_PAUSE_IR,
    JTAG_EXIT2_IR,
    JTAG_UPDATE_IR,
} jtag_state_t;

typedef struct {
    bool in_use;
    uint32_t bytes_received;

    char error[128];

    jtag_state_t current_state;
    size_t raw_bytes_remaining;
} jtag_session_t;

static const char *TAG = "jtag_player";
static jtag_session_t s_session = {
    .current_state = JTAG_TEST_LOGIC_RESET,
};
static SemaphoreHandle_t s_lock;

static const jtag_state_t s_jtag_next[16][2] = {
    [JTAG_TEST_LOGIC_RESET] = {JTAG_RUN_TEST_IDLE, JTAG_TEST_LOGIC_RESET},
    [JTAG_RUN_TEST_IDLE] = {JTAG_RUN_TEST_IDLE, JTAG_SELECT_DR_SCAN},
    [JTAG_SELECT_DR_SCAN] = {JTAG_CAPTURE_DR, JTAG_SELECT_IR_SCAN},
    [JTAG_CAPTURE_DR] = {JTAG_SHIFT_DR, JTAG_EXIT1_DR},
    [JTAG_SHIFT_DR] = {JTAG_SHIFT_DR, JTAG_EXIT1_DR},
    [JTAG_EXIT1_DR] = {JTAG_PAUSE_DR, JTAG_UPDATE_DR},
    [JTAG_PAUSE_DR] = {JTAG_PAUSE_DR, JTAG_EXIT2_DR},
    [JTAG_EXIT2_DR] = {JTAG_SHIFT_DR, JTAG_UPDATE_DR},
    [JTAG_UPDATE_DR] = {JTAG_RUN_TEST_IDLE, JTAG_SELECT_DR_SCAN},
    [JTAG_SELECT_IR_SCAN] = {JTAG_CAPTURE_IR, JTAG_TEST_LOGIC_RESET},
    [JTAG_CAPTURE_IR] = {JTAG_SHIFT_IR, JTAG_EXIT1_IR},
    [JTAG_SHIFT_IR] = {JTAG_SHIFT_IR, JTAG_EXIT1_IR},
    [JTAG_EXIT1_IR] = {JTAG_PAUSE_IR, JTAG_UPDATE_IR},
    [JTAG_PAUSE_IR] = {JTAG_PAUSE_IR, JTAG_EXIT2_IR},
    [JTAG_EXIT2_IR] = {JTAG_SHIFT_IR, JTAG_UPDATE_IR},
    [JTAG_UPDATE_IR] = {JTAG_RUN_TEST_IDLE, JTAG_SELECT_DR_SCAN},
};

static inline void jtag_set_tck(int level)
{
    gpio_set_level(CONFIG_LOADER_JTAG_GPIO_TCK, level);
}

static void set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_session.error, sizeof(s_session.error), fmt, args);
    va_end(args);
}

static int jtag_clock_bit(int tms, int tdi)
{
    gpio_set_level(CONFIG_LOADER_JTAG_GPIO_TMS, tms);
    gpio_set_level(CONFIG_LOADER_JTAG_GPIO_TDI, tdi);

    jtag_set_tck(1);
    int tdo = gpio_get_level(CONFIG_LOADER_JTAG_GPIO_TDO);
    jtag_set_tck(0);

    s_session.current_state = s_jtag_next[s_session.current_state][tms ? 1 : 0];
    return tdo;
}

static void jtag_move_to_reset(void)
{
    for (int i = 0; i < 6; ++i) {
        jtag_clock_bit(1, 0);
    }
}

static esp_err_t jtag_move_to(jtag_state_t target)
{
    if (target == JTAG_STATE_INVALID || target > JTAG_UPDATE_IR) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_session.current_state == target) {
        return ESP_OK;
    }

    int prev[16];
    int prev_tms[16];
    bool visited[16] = {0};
    int queue[16];
    int q_head = 0;
    int q_tail = 0;

    for (int i = 0; i < 16; ++i) {
        prev[i] = -1;
        prev_tms[i] = -1;
    }

    visited[s_session.current_state] = true;
    queue[q_tail++] = s_session.current_state;

    while (q_head < q_tail && !visited[target]) {
        int state = queue[q_head++];
        for (int tms = 0; tms <= 1; ++tms) {
            int next = s_jtag_next[state][tms];
            if (!visited[next]) {
                visited[next] = true;
                prev[next] = state;
                prev_tms[next] = tms;
                queue[q_tail++] = next;
            }
        }
    }

    if (!visited[target]) {
        return ESP_FAIL;
    }

    int tms_path[32];
    int path_len = 0;
    for (int node = target; node != s_session.current_state; node = prev[node]) {
        if (path_len >= (int)(sizeof(tms_path) / sizeof(tms_path[0]))) {
            return ESP_FAIL;
        }
        tms_path[path_len++] = prev_tms[node];
    }

    for (int i = path_len - 1; i >= 0; --i) {
        jtag_clock_bit(tms_path[i], 0);
    }

    return ESP_OK;
}

static void end_active_session(void)
{
    s_session.in_use = false;
    xSemaphoreGive(s_lock);
}

static esp_err_t shift_ir_u8(uint8_t value, int bit_len)
{
    if (bit_len <= 0 || bit_len > 8) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(jtag_move_to(JTAG_SHIFT_IR), TAG, "failed to move to SHIFT_IR");
    for (int bit = 0; bit < bit_len; ++bit) {
        bool last_bit = (bit == bit_len - 1);
        jtag_clock_bit(last_bit ? 1 : 0, (value >> bit) & 0x1);
    }

    ESP_RETURN_ON_ERROR(jtag_move_to(JTAG_RUN_TEST_IDLE), TAG, "failed to return to IDLE");
    return ESP_OK;
}

static esp_err_t begin_player_session(void)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(s_session.error, 0, sizeof(s_session.error));
    s_session.bytes_received = 0;
    s_session.raw_bytes_remaining = 0;
    s_session.current_state = JTAG_TEST_LOGIC_RESET;
    s_session.in_use = true;

    jtag_move_to_reset();
    esp_err_t err = jtag_move_to(JTAG_RUN_TEST_IDLE);
    if (err != ESP_OK) {
        set_error("failed to enter IDLE before playback");
        end_active_session();
        return err;
    }

    return ESP_OK;
}

esp_err_t jtag_player_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    const gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_LOADER_JTAG_GPIO_TCK) |
                        (1ULL << CONFIG_LOADER_JTAG_GPIO_TMS) |
                        (1ULL << CONFIG_LOADER_JTAG_GPIO_TDI),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out_cfg), TAG, "failed to configure JTAG output pins");

    const gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_LOADER_JTAG_GPIO_TDO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_cfg), TAG, "failed to configure JTAG input pin");

    jtag_set_tck(0);
    gpio_set_level(CONFIG_LOADER_JTAG_GPIO_TMS, 1);
    gpio_set_level(CONFIG_LOADER_JTAG_GPIO_TDI, 0);

    return ESP_OK;
}

esp_err_t raw_bitstream_player_begin(size_t total_bytes)
{
    if (total_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(begin_player_session(), TAG, "failed to begin session");
    s_session.raw_bytes_remaining = total_bytes;

    esp_err_t err = shift_ir_u8(0x0B, 6);
    if (err != ESP_OK) {
        set_error("failed to execute JPROGRAM");
        end_active_session();
        return err;
    }

    err = shift_ir_u8(0x14, 6);
    if (err != ESP_OK) {
        set_error("failed to execute ISC_NOOP");
        end_active_session();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    err = shift_ir_u8(0x05, 6);
    if (err != ESP_OK) {
        set_error("failed to execute CFG_IN");
        end_active_session();
        return err;
    }

    err = jtag_move_to(JTAG_SHIFT_DR);
    if (err != ESP_OK) {
        set_error("failed to enter SHIFT_DR");
        end_active_session();
        return err;
    }
    return ESP_OK;
}

esp_err_t raw_bitstream_player_feed(const uint8_t *data, size_t len, bool is_last_chunk)
{
    if (!s_session.in_use) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0 || len > s_session.raw_bytes_remaining) {
        set_error("invalid raw bitstream chunk");
        end_active_session();
        return ESP_ERR_INVALID_ARG;
    }
    if (is_last_chunk != (len == s_session.raw_bytes_remaining)) {
        set_error("raw bitstream chunking mismatch");
        end_active_session();
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = data[i];
        for (int bit = 7; bit >= 0; --bit) {
            bool is_last_bit = is_last_chunk && (i == len - 1) && (bit == 0);
            jtag_clock_bit(is_last_bit ? 1 : 0, (byte >> bit) & 0x1);
        }
    }

    s_session.bytes_received += (uint32_t)len;
    s_session.raw_bytes_remaining -= len;
    return ESP_OK;
}

esp_err_t raw_bitstream_player_finish(void)
{
    if (!s_session.in_use) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_session.raw_bytes_remaining != 0) {
        set_error("raw bitstream upload ended early");
        end_active_session();
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = jtag_move_to(JTAG_RUN_TEST_IDLE);
    if (err != ESP_OK) {
        set_error("failed to exit SHIFT_DR");
        end_active_session();
        return err;
    }

    err = shift_ir_u8(0x0C, 6);
    if (err != ESP_OK) {
        set_error("failed to execute JSTART");
        end_active_session();
        return err;
    }

    for (uint32_t i = 0; i < 128; ++i) {
        jtag_clock_bit(0, 0);
    }

    end_active_session();
    return ESP_OK;
}

void raw_bitstream_player_abort(void)
{
    if (s_session.in_use) {
        end_active_session();
    }
}

const char *jtag_player_last_error(void)
{
    if (s_session.error[0] == '\0') {
        return "unknown error";
    }
    return s_session.error;
}
