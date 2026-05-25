#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"

#include "siggen_http.h"
#include "siggen_spi.h"

typedef struct {
    float freq_hz;
    uint8_t waveform;
    float amplitude;
    float duty;
    bool enable;
} siggen_channel_state_t;

static const char *TAG = "siggen_http";
static siggen_channel_state_t s_state[2] = {
    {.freq_hz = 1000.0f, .waveform = SIGGEN_WAVE_SINE, .amplitude = 0.8f, .duty = 0.5f, .enable = true},
    {.freq_hz = 1000.0f, .waveform = SIGGEN_WAVE_SINE, .amplitude = 0.8f, .duty = 0.5f, .enable = true},
};

static const char s_index_html[] =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>GalvDrv SigGen</title><style>body{font-family:sans-serif;max-width:700px;margin:0 auto;padding:1rem;background:#f5f6fa;color:#111}"
".card{background:#fff;border-radius:10px;padding:1rem;box-shadow:0 2px 8px rgba(0,0,0,.08)}label{display:block;margin:.6rem 0 .2rem}"
"input,select{width:100%;padding:.5rem;border:1px solid #ccd;border-radius:8px}input[type=range]{padding:0}"
".row{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}.hint{font-size:.9rem;color:#555}.hidden{display:none}</style></head><body>"
"<div class='card'><h2>Dual DDS Signal Generator</h2><div class='row'><div><label>Channel</label><select id='channel'><option value='0'>Left (Ch0)</option>"
"<option value='1'>Right (Ch1)</option><option value='2'>Both</option></select></div><div><label>Waveform</label><select id='waveform'><option>sine</option><option>square</option><option>ramp</option><option>triangle</option></select></div></div>"
"<label>Frequency (Hz)</label><input id='freq' type='number' min='0.001' max='62500' step='0.001'><div class='hint'>Range: 0.001 - 62500 Hz</div>"
"<label>Amplitude (<span id='ampv'>0</span>%)</label><input id='amp' type='range' min='0' max='100' step='0.1'>"
"<div id='dutyWrap'><label>Duty (<span id='dutyv'>0</span>%)</label><input id='duty' type='range' min='0' max='100' step='0.1'></div>"
"<label><input id='enable' type='checkbox' style='width:auto'> Enabled</label><div class='hint' id='status'>Loading…</div></div>"
"<script>const el=id=>document.getElementById(id);const S={ch:[{},{}]};"
"function cur(){const c=+el('channel').value;return c===2?S.ch[0]:S.ch[c]}"
"function syncUi(){const st=cur();el('waveform').value=st.waveform||'sine';el('freq').value=(st.freq_hz||1000).toFixed(3);"
"el('amp').value=((st.amplitude||0)*100).toFixed(1);el('ampv').textContent=el('amp').value;el('duty').value=((st.duty||0)*100).toFixed(1);"
"el('dutyv').textContent=el('duty').value;el('enable').checked=!!st.enable;el('dutyWrap').className=el('waveform').value==='square'?'':'hidden'}"
"async function load(){const r=await fetch('/api/siggen');const j=await r.json();S.ch=j.channels;syncUi();el('status').textContent='Ready';}"
"async function push(){const p={channel:+el('channel').value,waveform:el('waveform').value,freq_hz:+el('freq').value,amplitude:+el('amp').value/100,duty:+el('duty').value/100,enable:el('enable').checked};"
"el('status').textContent='Updating…';await fetch('/api/siggen',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});await load();}"
"['channel','waveform','freq','amp','duty','enable'].forEach(id=>el(id).addEventListener('change',()=>{if(id==='channel'){syncUi();return;}if(id==='waveform')el('dutyWrap').className=el('waveform').value==='square'?'':'hidden';if(id==='amp')el('ampv').textContent=el('amp').value;if(id==='duty')el('dutyv').textContent=el('duty').value;push();}));"
"load().catch(e=>{el('status').textContent='Error: '+e.message;});</script></body></html>";

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static const char *waveform_to_name(uint8_t waveform)
{
    switch (waveform) {
        case SIGGEN_WAVE_SINE: return "sine";
        case SIGGEN_WAVE_SQUARE: return "square";
        case SIGGEN_WAVE_RAMP: return "ramp";
        case SIGGEN_WAVE_TRIANGLE: return "triangle";
        default: return "sine";
    }
}

static bool waveform_from_name(const char *name, uint8_t *out_waveform)
{
    if (!name || !out_waveform) {
        return false;
    }

    if (strcmp(name, "sine") == 0) {
        *out_waveform = SIGGEN_WAVE_SINE;
    } else if (strcmp(name, "square") == 0) {
        *out_waveform = SIGGEN_WAVE_SQUARE;
    } else if (strcmp(name, "ramp") == 0) {
        *out_waveform = SIGGEN_WAVE_RAMP;
    } else if (strcmp(name, "triangle") == 0) {
        *out_waveform = SIGGEN_WAVE_TRIANGLE;
    } else {
        return false;
    }

    return true;
}

static esp_err_t apply_channel_state(uint8_t channel)
{
    const siggen_channel_state_t *st = &s_state[channel];
    uint16_t amp_u16 = (uint16_t)(clampf(st->amplitude, 0.0f, 1.0f) * 65535.0f + 0.5f);
    uint16_t duty_u16 = (uint16_t)(clampf(st->duty, 0.0f, 1.0f) * 65535.0f + 0.5f);

    ESP_RETURN_ON_ERROR(siggen_set_freq(channel, st->freq_hz), TAG, "set freq failed");
    ESP_RETURN_ON_ERROR(siggen_set_waveform(channel, st->waveform), TAG, "set waveform failed");
    ESP_RETURN_ON_ERROR(siggen_set_amplitude(channel, amp_u16), TAG, "set amplitude failed");
    ESP_RETURN_ON_ERROR(siggen_set_duty(channel, duty_u16), TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(siggen_enable(channel, st->enable), TAG, "set enable failed");
    return ESP_OK;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t siggen_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *channels = cJSON_AddArrayToObject(root, "channels");

    for (int i = 0; i < 2; ++i) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "channel", i);
        cJSON_AddStringToObject(ch, "waveform", waveform_to_name(s_state[i].waveform));
        cJSON_AddNumberToObject(ch, "freq_hz", s_state[i].freq_hz);
        cJSON_AddNumberToObject(ch, "amplitude", s_state[i].amplitude);
        cJSON_AddNumberToObject(ch, "duty", s_state[i].duty);
        cJSON_AddBoolToObject(ch, "enable", s_state[i].enable);
        cJSON_AddItemToArray(channels, ch);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
    }

    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t siggen_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (req->content_len <= 0 || req->content_len > 1024) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid body size\"}");
    }

    char *body = calloc((size_t)req->content_len + 1, 1);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    const cJSON *channel_item = cJSON_GetObjectItemCaseSensitive(root, "channel");
    if (!cJSON_IsNumber(channel_item)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "channel required");
    }

    int channel = channel_item->valueint;
    if (channel < 0 || channel > 2) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "channel out of range");
    }

    uint8_t channel_start = (channel == 2) ? 0 : (uint8_t)channel;
    uint8_t channel_end = (channel == 2) ? 1 : (uint8_t)channel;

    const cJSON *wave_item = cJSON_GetObjectItemCaseSensitive(root, "waveform");
    const cJSON *freq_item = cJSON_GetObjectItemCaseSensitive(root, "freq_hz");
    const cJSON *amp_item = cJSON_GetObjectItemCaseSensitive(root, "amplitude");
    const cJSON *duty_item = cJSON_GetObjectItemCaseSensitive(root, "duty");
    const cJSON *enable_item = cJSON_GetObjectItemCaseSensitive(root, "enable");

    for (uint8_t ch = channel_start; ch <= channel_end; ++ch) {
        if (cJSON_IsString(wave_item)) {
            uint8_t waveform;
            if (!waveform_from_name(wave_item->valuestring, &waveform)) {
                cJSON_Delete(root);
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid waveform");
            }
            s_state[ch].waveform = waveform;
        }

        if (cJSON_IsNumber(freq_item)) {
            s_state[ch].freq_hz = clampf((float)freq_item->valuedouble, 0.0f, (float)SIGGEN_MAX_FREQ_HZ);
        }

        if (cJSON_IsNumber(amp_item)) {
            s_state[ch].amplitude = clampf((float)amp_item->valuedouble, 0.0f, 1.0f);
        }

        if (cJSON_IsNumber(duty_item)) {
            s_state[ch].duty = clampf((float)duty_item->valuedouble, 0.0f, 1.0f);
        }

        if (cJSON_IsBool(enable_item)) {
            s_state[ch].enable = cJSON_IsTrue(enable_item);
        }

        esp_err_t err = apply_channel_state(ch);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            ESP_LOGE(TAG, "apply channel %u failed: %s", ch, esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "spi write failed");
        }
    }

    cJSON_Delete(root);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t siggen_http_register(httpd_handle_t server)
{
    ESP_RETURN_ON_ERROR(apply_channel_state(0), TAG, "init channel 0 failed");
    ESP_RETURN_ON_ERROR(apply_channel_state(1), TAG, "init channel 1 failed");

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t siggen_get_uri = {
        .uri = "/api/siggen",
        .method = HTTP_GET,
        .handler = siggen_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t siggen_post_uri = {
        .uri = "/api/siggen",
        .method = HTTP_POST,
        .handler = siggen_post_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &index_uri), TAG, "register / failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &siggen_get_uri), TAG, "register GET /api/siggen failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &siggen_post_uri), TAG, "register POST /api/siggen failed");

    return ESP_OK;
}
