#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
    uint32_t statements_executed;
    uint32_t sir_commands;
    uint32_t sdr_commands;

    char *statement;
    size_t statement_len;
    size_t statement_cap;

    char error[128];

    jtag_state_t current_state;
    jtag_state_t endir_state;
    jtag_state_t enddr_state;
    uint32_t tck_hz;
    size_t raw_bytes_remaining;
} svf_session_t;

static const char *TAG = "jtag_player";
static svf_session_t s_session = {
    .endir_state = JTAG_RUN_TEST_IDLE,
    .enddr_state = JTAG_RUN_TEST_IDLE,
    .current_state = JTAG_TEST_LOGIC_RESET,
    .tck_hz = CONFIG_LOADER_JTAG_TCK_HZ,
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

static inline int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static inline int hex_bit_at_lsb_first(const char *hex, size_t hex_len, int bit_index)
{
    if (bit_index < 0) {
        return 0;
    }

    size_t nibble_offset = (size_t)bit_index / 4;
    if (nibble_offset >= hex_len) {
        return 0;
    }

    size_t char_index = hex_len - 1 - nibble_offset;
    int nibble = hex_nibble(hex[char_index]);
    if (nibble < 0) {
        return 0;
    }

    return (nibble >> (bit_index % 4)) & 0x1;
}

static jtag_state_t parse_state_token(const char *token)
{
    if (strcmp(token, "RESET") == 0 || strcmp(token, "TEST_LOGIC_RESET") == 0) {
        return JTAG_TEST_LOGIC_RESET;
    }
    if (strcmp(token, "IDLE") == 0 || strcmp(token, "RUN_TEST_IDLE") == 0 || strcmp(token, "RUNTEST") == 0) {
        return JTAG_RUN_TEST_IDLE;
    }
    if (strcmp(token, "DRPAUSE") == 0 || strcmp(token, "PAUSE_DR") == 0) {
        return JTAG_PAUSE_DR;
    }
    if (strcmp(token, "IRPAUSE") == 0 || strcmp(token, "PAUSE_IR") == 0) {
        return JTAG_PAUSE_IR;
    }
    if (strcmp(token, "SHIFTDR") == 0 || strcmp(token, "SHIFT_DR") == 0) {
        return JTAG_SHIFT_DR;
    }
    if (strcmp(token, "SHIFTIR") == 0 || strcmp(token, "SHIFT_IR") == 0) {
        return JTAG_SHIFT_IR;
    }
    return JTAG_STATE_INVALID;
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

static bool parse_hex_arg(const char *statement, const char *keyword, const char **hex_start, size_t *hex_len)
{
    const char *k = strstr(statement, keyword);
    if (!k) {
        return false;
    }

    const char *open = strchr(k, '(');
    if (!open) {
        return false;
    }

    const char *close = strchr(open + 1, ')');
    if (!close || close <= open + 1) {
        return false;
    }

    *hex_start = open + 1;
    *hex_len = (size_t)(close - open - 1);
    return true;
}

static void trim_hex(const char **hex_start, size_t *hex_len)
{
    while (*hex_len > 0 && isspace((unsigned char)(*hex_start)[0])) {
        (*hex_start)++;
        (*hex_len)--;
    }
    while (*hex_len > 0 && isspace((unsigned char)(*hex_start)[*hex_len - 1])) {
        (*hex_len)--;
    }
}

static esp_err_t jtag_shift(bool is_ir,
                            int bit_len,
                            const char *tdi_hex,
                            size_t tdi_len,
                            const char *tdo_hex,
                            size_t tdo_len,
                            const char *mask_hex,
                            size_t mask_len)
{
    if (bit_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(jtag_move_to(is_ir ? JTAG_SHIFT_IR : JTAG_SHIFT_DR), TAG, "failed to move to shift state");

    for (int bit = 0; bit < bit_len; ++bit) {
        int tdi_bit = hex_bit_at_lsb_first(tdi_hex, tdi_len, bit);
        bool last_bit = (bit == (bit_len - 1));
        int tdo_bit = jtag_clock_bit(last_bit ? 1 : 0, tdi_bit);

        if (tdo_hex) {
            int expected = hex_bit_at_lsb_first(tdo_hex, tdo_len, bit);
            int mask = mask_hex ? hex_bit_at_lsb_first(mask_hex, mask_len, bit) : 1;
            if (mask && (expected != tdo_bit)) {
                set_error("TDO mismatch on bit %d (expected=%d got=%d)", bit, expected, tdo_bit);
                return ESP_FAIL;
            }
        }
    }

    ESP_RETURN_ON_ERROR(jtag_move_to(is_ir ? s_session.endir_state : s_session.enddr_state), TAG, "failed to move to end state");
    return ESP_OK;
}

static esp_err_t parse_and_execute_shift(const char *statement, bool is_ir)
{
    const char *cursor = statement;
    while (*cursor && !isspace((unsigned char)*cursor)) {
        cursor++;
    }
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    char *endptr = NULL;
    long bit_len_long = strtol(cursor, &endptr, 10);
    if (endptr == cursor || bit_len_long <= 0 || bit_len_long > INT32_MAX) {
        set_error("invalid bit length in %s", is_ir ? "SIR" : "SDR");
        return ESP_ERR_INVALID_ARG;
    }
    int bit_len = (int)bit_len_long;

    const char *tdi_hex = NULL;
    size_t tdi_len = 0;
    if (!parse_hex_arg(statement, "TDI", &tdi_hex, &tdi_len)) {
        set_error("missing TDI argument");
        return ESP_ERR_INVALID_ARG;
    }
    trim_hex(&tdi_hex, &tdi_len);

    const char *tdo_hex = NULL;
    size_t tdo_len = 0;
    if (parse_hex_arg(statement, "TDO", &tdo_hex, &tdo_len)) {
        trim_hex(&tdo_hex, &tdo_len);
    }

    const char *mask_hex = NULL;
    size_t mask_len = 0;
    if (parse_hex_arg(statement, "MASK", &mask_hex, &mask_len)) {
        trim_hex(&mask_hex, &mask_len);
    }

    return jtag_shift(is_ir, bit_len, tdi_hex, tdi_len, tdo_hex, tdo_len, mask_hex, mask_len);
}

static esp_err_t parse_and_execute_runtest(const char *statement)
{
    char local[192];
    strlcpy(local, statement, sizeof(local));

    char *saveptr = NULL;
    char *token = strtok_r(local, " \t\r\n;", &saveptr); // RUNTEST
    (void)token;

    token = strtok_r(NULL, " \t\r\n;", &saveptr);
    if (!token) {
        set_error("invalid RUNTEST statement");
        return ESP_ERR_INVALID_ARG;
    }

    jtag_state_t run_state = JTAG_RUN_TEST_IDLE;
    char *end_check = NULL;
    double value = strtod(token, &end_check);
    if (end_check == token || *end_check != '\0') {
        run_state = parse_state_token(token);
        if (run_state == JTAG_STATE_INVALID) {
            set_error("unsupported RUNTEST state: %s", token);
            return ESP_ERR_INVALID_ARG;
        }
        token = strtok_r(NULL, " \t\r\n;", &saveptr);
        if (!token) {
            set_error("missing RUNTEST cycle/time value");
            return ESP_ERR_INVALID_ARG;
        }
        value = strtod(token, &end_check);
    }

    if (end_check == token || *end_check != '\0' || value < 0.0) {
        set_error("invalid RUNTEST value");
        return ESP_ERR_INVALID_ARG;
    }

    token = strtok_r(NULL, " \t\r\n;", &saveptr);
    uint32_t cycles = 0;
    if (token && strcmp(token, "SEC") == 0) {
        cycles = (uint32_t)(value * (double)s_session.tck_hz);
    } else {
        cycles = (uint32_t)value;
    }

    ESP_RETURN_ON_ERROR(jtag_move_to(run_state), TAG, "failed to move to RUNTEST state");

    for (uint32_t i = 0; i < cycles; ++i) {
        jtag_clock_bit(0, 0);
    }

    return ESP_OK;
}

static esp_err_t parse_and_execute_frequency(const char *statement)
{
    const char *cursor = statement + strlen("FREQUENCY");
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        set_error("FREQUENCY requires a value");
        return ESP_ERR_INVALID_ARG;
    }

    char *endptr = NULL;
    double hz = strtod(cursor, &endptr);
    if (endptr == cursor || hz <= 0.0) {
        set_error("invalid FREQUENCY value");
        return ESP_ERR_INVALID_ARG;
    }

    while (*endptr && (isspace((unsigned char)*endptr) || *endptr == ';')) {
        endptr++;
    }
    if (strncmp(endptr, "HZ", 2) == 0) {
        endptr += 2;
        while (*endptr && (isspace((unsigned char)*endptr) || *endptr == ';')) {
            endptr++;
        }
    }
    if (*endptr != '\0') {
        set_error("unsupported FREQUENCY unit: %s", endptr);
        return ESP_ERR_INVALID_ARG;
    }

    s_session.tck_hz = (uint32_t)hz;
    ESP_LOGI(TAG, "SVF FREQUENCY set TCK to %u Hz", s_session.tck_hz);
    return ESP_OK;
}

static esp_err_t parse_and_execute_state(const char *statement)
{
    char local[128];
    strlcpy(local, statement, sizeof(local));

    char *saveptr = NULL;
    char *token = strtok_r(local, " \t\r\n;", &saveptr);
    token = strtok_r(NULL, " \t\r\n;", &saveptr);

    if (!token) {
        set_error("STATE requires at least one target state");
        return ESP_ERR_INVALID_ARG;
    }

    while (token) {
        jtag_state_t state = parse_state_token(token);
        if (state == JTAG_STATE_INVALID) {
            set_error("unsupported STATE target: %s", token);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(jtag_move_to(state), TAG, "state transition failed");
        token = strtok_r(NULL, " \t\r\n;", &saveptr);
    }

    return ESP_OK;
}

static esp_err_t parse_end_state(const char *statement, bool is_ir)
{
    char local[64];
    strlcpy(local, statement, sizeof(local));

    char *saveptr = NULL;
    char *token = strtok_r(local, " \t\r\n;", &saveptr);
    token = strtok_r(NULL, " \t\r\n;", &saveptr);
    if (!token) {
        set_error("missing state for %s", is_ir ? "ENDIR" : "ENDDR");
        return ESP_ERR_INVALID_ARG;
    }

    jtag_state_t state = parse_state_token(token);
    if (state == JTAG_STATE_INVALID) {
        set_error("invalid %s state: %s", is_ir ? "ENDIR" : "ENDDR", token);
        return ESP_ERR_INVALID_ARG;
    }

    if (is_ir) {
        s_session.endir_state = state;
    } else {
        s_session.enddr_state = state;
    }

    return ESP_OK;
}

static esp_err_t execute_statement(char *statement)
{
    char compact[16] = {0};
    size_t compact_len = 0;
    for (size_t i = 0; statement[i] != '\0'; ++i) {
        if (!isspace((unsigned char)statement[i])) {
            compact[compact_len++] = (char)toupper((unsigned char)statement[i]);
            if (compact_len == sizeof(compact) - 1) {
                break;
            }
        }
    }

    if (compact_len == 0) {
        return ESP_OK;
    }

    for (char *p = statement; *p; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }
ESP_LOGI(TAG, "SVF: %s", statement);
    if (strncmp(compact, "SIR", 3) == 0) {
        ESP_RETURN_ON_ERROR(parse_and_execute_shift(statement, true), TAG, "failed to execute SIR");
        s_session.sir_commands++;
    } else if (strncmp(compact, "SDR", 3) == 0) {
        ESP_RETURN_ON_ERROR(parse_and_execute_shift(statement, false), TAG, "failed to execute SDR");
        s_session.sdr_commands++;
    } else if (strncmp(compact, "RUNTEST", 7) == 0) {
        ESP_RETURN_ON_ERROR(parse_and_execute_runtest(statement), TAG, "failed to execute RUNTEST");
    } else if (strncmp(compact, "STATE", 5) == 0) {
        ESP_RETURN_ON_ERROR(parse_and_execute_state(statement), TAG, "failed to execute STATE");
    } else if (strncmp(compact, "ENDIR", 5) == 0) {
        ESP_RETURN_ON_ERROR(parse_end_state(statement, true), TAG, "failed to execute ENDIR");
    } else if (strncmp(compact, "ENDDR", 5) == 0) {
        ESP_RETURN_ON_ERROR(parse_end_state(statement, false), TAG, "failed to execute ENDDR");
    } else if (strncmp(compact, "HIR", 3) == 0 || strncmp(compact, "HDR", 3) == 0 ||
               strncmp(compact, "TIR", 3) == 0 || strncmp(compact, "TDR", 3) == 0) {
        ESP_LOGD(TAG, "Ignoring %s command for single-device chain", compact);
    } else if (strncmp(compact, "FREQUENCY", 9) == 0) {
        ESP_RETURN_ON_ERROR(parse_and_execute_frequency(statement), TAG, "failed to execute FREQUENCY");
    } else if (strncmp(compact, "TRST", 4) == 0) {
        ESP_LOGD(TAG, "Ignoring TRST command");
    } else {
        set_error("unsupported SVF command: %.24s", statement);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_session.statements_executed++;
    return ESP_OK;
}

static esp_err_t process_statement_buffer(void)
{
    if (s_session.statement_len == 0) {
        return ESP_OK;
    }

    s_session.statement[s_session.statement_len] = '\0';

    size_t write_pos = 0;
    bool bang_comment = false;
    bool slash_comment = false;
    for (size_t i = 0; i < s_session.statement_len; ++i) {
        char c = s_session.statement[i];

        if (bang_comment) {
            if (c == '\n' || c == '\r') {
                bang_comment = false;
            }
            continue;
        }

        if (slash_comment) {
            if (c == '\n' || c == '\r') {
                slash_comment = false;
            }
            continue;
        }

        if (c == '!') {
            bang_comment = true;
            continue;
        }

        if (c == '/' && (i + 1) < s_session.statement_len && s_session.statement[i + 1] == '/') {
            slash_comment = true;
            i++;
            continue;
        }

        s_session.statement[write_pos++] = c;
    }

    s_session.statement[write_pos] = '\0';
    while (write_pos > 0 && isspace((unsigned char)s_session.statement[write_pos - 1])) {
        s_session.statement[--write_pos] = '\0';
    }

    // skip leading whitespace
    size_t start = 0;
    while (start < write_pos && isspace((unsigned char)s_session.statement[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s_session.statement, s_session.statement + start, write_pos - start + 1);
    }

    return execute_statement(s_session.statement);
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
    s_session.statements_executed = 0;
    s_session.sir_commands = 0;
    s_session.sdr_commands = 0;
    s_session.statement_len = 0;
    s_session.endir_state = JTAG_RUN_TEST_IDLE;
    s_session.enddr_state = JTAG_RUN_TEST_IDLE;
    s_session.tck_hz = CONFIG_LOADER_JTAG_TCK_HZ;
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

esp_err_t svf_player_begin(void)
{
    ESP_RETURN_ON_ERROR(begin_player_session(), TAG, "failed to begin session");

    if (!s_session.statement) {
        s_session.statement_cap = 4096;
        s_session.statement = calloc(1, s_session.statement_cap);
        if (!s_session.statement) {
            end_active_session();
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t svf_player_feed(const uint8_t *data, size_t len)
{
    if (!s_session.in_use) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        s_session.bytes_received++;

        if (s_session.statement_len + 2 >= s_session.statement_cap) {
            size_t new_cap = s_session.statement_cap * 2;
            if (new_cap > CONFIG_LOADER_MAX_STATEMENT_BYTES) {
                set_error("SVF statement exceeds configured maximum");
                end_active_session();
                return ESP_ERR_INVALID_SIZE;
            }
            char *new_buf = realloc(s_session.statement, new_cap);
            if (!new_buf) {
                set_error("out of memory while growing statement buffer");
                end_active_session();
                return ESP_ERR_NO_MEM;
            }
            s_session.statement = new_buf;
            s_session.statement_cap = new_cap;
        }

        s_session.statement[s_session.statement_len++] = c;

        if (c == ';') {
            esp_err_t err = process_statement_buffer();
            s_session.statement_len = 0;
            if (err != ESP_OK) {
                end_active_session();
                return err;
            }
        }
    }

    return ESP_OK;
}

esp_err_t svf_player_finish(svf_result_t *result)
{
    if (!s_session.in_use) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session.statement_len != 0) {
        set_error("SVF ended with incomplete statement (missing ';')");
        if (result) {
            memset(result, 0, sizeof(*result));
            result->bytes_received = s_session.bytes_received;
            strlcpy(result->message, s_session.error, sizeof(result->message));
        }
        s_session.in_use = false;
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (result) {
        memset(result, 0, sizeof(*result));
        result->success = true;
        result->bytes_received = s_session.bytes_received;
        result->statements_executed = s_session.statements_executed;
        result->sir_commands = s_session.sir_commands;
        result->sdr_commands = s_session.sdr_commands;
        strlcpy(result->message, "OK", sizeof(result->message));
    }

    end_active_session();
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

    for (uint32_t i = 0; i < 1024; ++i) {
        jtag_clock_bit(0, 0);
    }

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

    for (uint32_t i = 0; i < 32; ++i) {
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

const char *svf_player_last_error(void)
{
    if (s_session.error[0] == '\0') {
        return "unknown error";
    }
    return s_session.error;
}
