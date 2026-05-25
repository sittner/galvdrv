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
    {.freq_hz = 0.0f, .waveform = SIGGEN_WAVE_SINE, .amplitude = 0.0f, .duty = 0.5f, .enable = false},
    {.freq_hz = 0.0f, .waveform = SIGGEN_WAVE_SINE, .amplitude = 0.0f, .duty = 0.5f, .enable = false},
};

static const char s_index_html[] =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>GalvDrv SigGen</title><style>body{font-family:sans-serif;max-width:1100px;margin:0 auto;padding:1rem;background:#f5f6fa;color:#111}"
".card{background:#fff;border-radius:10px;padding:1rem;box-shadow:0 2px 8px rgba(0,0,0,.08)}label{display:block;margin:.6rem 0 .2rem;font-size:.95rem}"
"input,select{width:100%;padding:.5rem;border:1px solid #ccd;border-radius:8px}input[type=range]{padding:0}"
".channels{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}.hint{font-size:.9rem;color:#555}.muted{color:#666}.hidden{display:none}"
"@media (max-width:900px){.channels{grid-template-columns:1fr}}</style></head><body>"
"<div class='card'><h2>Dual DDS Signal Generator</h2><div class='hint'>Use mouse wheel on numeric controls to adjust quickly.</div><div class='hint' id='status'>Loading…</div></div>"
"<div class='channels'>"
"<div class='card'><h3>Left (Ch0)</h3><label>Waveform</label><select id='ch0_waveform'><option>sine</option><option>square</option><option>ramp</option><option>triangle</option></select>"
"<label>Frequency (Hz)</label><input id='ch0_freq' type='number' min='0' max='62500' step='0.001'><div class='hint muted'>Range: 0 - 62500 Hz</div>"
"<label>Amplitude (<span id='ch0_ampv'>0</span>%)</label><input id='ch0_amp' type='range' min='0' max='100' step='0.1'>"
"<div id='ch0_dutyWrap'><label>Duty (<span id='ch0_dutyv'>0</span>%)</label><input id='ch0_duty' type='range' min='0' max='100' step='0.1'></div>"
"<label><input id='ch0_enable' type='checkbox' style='width:auto'> Enabled</label></div>"
"<div class='card'><h3>Right (Ch1)</h3><label>Waveform</label><select id='ch1_waveform'><option>sine</option><option>square</option><option>ramp</option><option>triangle</option></select>"
"<label>Frequency (Hz)</label><input id='ch1_freq' type='number' min='0' max='62500' step='0.001'><div class='hint muted'>Range: 0 - 62500 Hz</div>"
"<label>Amplitude (<span id='ch1_ampv'>0</span>%)</label><input id='ch1_amp' type='range' min='0' max='100' step='0.1'>"
"<div id='ch1_dutyWrap'><label>Duty (<span id='ch1_dutyv'>0</span>%)</label><input id='ch1_duty' type='range' min='0' max='100' step='0.1'></div>"
"<label><input id='ch1_enable' type='checkbox' style='width:auto'> Enabled</label></div>"
"</div>"
"<script>const el=id=>document.getElementById(id);const S={ch:[{},{}]};"
"const card=(ch,id)=>el(`ch${ch}_${id}`);"
"function showDuty(ch){el(`ch${ch}_dutyWrap`).className=card(ch,'waveform').value==='square'?'':'hidden'}"
"function syncCard(ch){const st=S.ch[ch]||{};card(ch,'waveform').value=st.waveform||'sine';card(ch,'freq').value=(st.freq_hz||0).toFixed(3);"
"card(ch,'amp').value=((st.amplitude||0)*100).toFixed(1);el(`ch${ch}_ampv`).textContent=card(ch,'amp').value;"
"card(ch,'duty').value=((st.duty||0.5)*100).toFixed(1);el(`ch${ch}_dutyv`).textContent=card(ch,'duty').value;"
"card(ch,'enable').checked=!!st.enable;showDuty(ch)}"
"async function load(){const r=await fetch('/api/siggen');const j=await r.json();S.ch=j.channels||S.ch;syncCard(0);syncCard(1);el('status').textContent='Ready';}"
"async function push(ch){const p={channel:ch,waveform:card(ch,'waveform').value,freq_hz:+card(ch,'freq').value,amplitude:+card(ch,'amp').value/100,duty:+card(ch,'duty').value/100,enable:card(ch,'enable').checked};"
"el('status').textContent='Updating…';const r=await fetch('/api/siggen',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});if(!r.ok)throw new Error(await r.text());await load();}"
"function wheelAdjust(input,deltaY){const step=parseFloat(input.step)||1;const min=parseFloat(input.min);const max=parseFloat(input.max);const cur=parseFloat(input.value)||0;"
"const decimals=(input.step&&input.step.includes('.'))?input.step.split('.')[1].length:0;let next=cur+(deltaY<0?step:-step);if(!Number.isNaN(min))next=Math.max(min,next);if(!Number.isNaN(max))next=Math.min(max,next);input.value=next.toFixed(decimals)}"
"function bindCard(ch){['waveform','freq','amp','duty','enable'].forEach(id=>card(ch,id).addEventListener('change',async()=>{if(id==='amp')el(`ch${ch}_ampv`).textContent=card(ch,'amp').value;"
"if(id==='duty')el(`ch${ch}_dutyv`).textContent=card(ch,'duty').value;if(id==='waveform')showDuty(ch);try{await push(ch);}catch(e){el('status').textContent='Error: '+e.message;}}));"
"['freq','amp','duty'].forEach(id=>card(ch,id).addEventListener('wheel',async e=>{e.preventDefault();wheelAdjust(card(ch,id),e.deltaY);"
"if(id==='amp')el(`ch${ch}_ampv`).textContent=card(ch,'amp').value;if(id==='duty')el(`ch${ch}_dutyv`).textContent=card(ch,'duty').value;try{await push(ch);}catch(err){el('status').textContent='Error: '+err.message;}},{passive:false}));}"
"bindCard(0);bindCard(1);load().catch(e=>{el('status').textContent='Error: '+e.message;});</script></body></html>";

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

static inline uint8_t channel_base(uint8_t channel)
{
    return channel == 0 ? 0x00 : 0x08;
}

static void set_safe_default_state(void)
{
    for (uint8_t ch = 0; ch < 2; ++ch) {
        s_state[ch].freq_hz = 0.0f;
        s_state[ch].waveform = SIGGEN_WAVE_SINE;
        s_state[ch].amplitude = 0.0f;
        s_state[ch].duty = 0.5f;
        s_state[ch].enable = false;
    }
}

static esp_err_t sync_state_from_fpga(void)
{
    esp_err_t err = ESP_OK;
    uint16_t enable_mask = 0;
    err = siggen_read_reg(0x10, &enable_mask);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read enable mask failed (%s), using safe defaults", esp_err_to_name(err));
        set_safe_default_state();
        return ESP_OK;
    }

    for (uint8_t ch = 0; ch < 2; ++ch) {
        uint8_t base = channel_base(ch);
        uint16_t phase_lo = 0;
        uint16_t phase_hi = 0;
        uint16_t waveform = 0;
        uint16_t amplitude = 0;
        uint16_t duty = 0;

        err = siggen_read_reg(base + 0x00, &phase_lo);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read ch%u phase low failed (%s), using safe defaults", ch, esp_err_to_name(err));
            set_safe_default_state();
            return ESP_OK;
        }
        err = siggen_read_reg(base + 0x01, &phase_hi);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read ch%u phase high failed (%s), using safe defaults", ch, esp_err_to_name(err));
            set_safe_default_state();
            return ESP_OK;
        }
        err = siggen_read_reg(base + 0x02, &waveform);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read ch%u waveform failed (%s), using safe defaults", ch, esp_err_to_name(err));
            set_safe_default_state();
            return ESP_OK;
        }
        err = siggen_read_reg(base + 0x03, &amplitude);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read ch%u amplitude failed (%s), using safe defaults", ch, esp_err_to_name(err));
            set_safe_default_state();
            return ESP_OK;
        }
        err = siggen_read_reg(base + 0x04, &duty);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read ch%u duty failed (%s), using safe defaults", ch, esp_err_to_name(err));
            set_safe_default_state();
            return ESP_OK;
        }

        uint32_t phase_inc = ((uint32_t)phase_hi << 16) | phase_lo;
        s_state[ch].freq_hz = (float)((double)phase_inc * (SIGGEN_SAMPLE_RATE_HZ / 4294967296.0));
        s_state[ch].waveform = (uint8_t)(waveform & 0x3u);
        s_state[ch].amplitude = (float)amplitude / 65535.0f;
        s_state[ch].duty = (float)duty / 65535.0f;
        s_state[ch].enable = ((enable_mask >> ch) & 0x1u) != 0;
    }

    return ESP_OK;
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
    ESP_RETURN_ON_ERROR(sync_state_from_fpga(), TAG, "sync from fpga failed");

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
    ESP_RETURN_ON_ERROR(sync_state_from_fpga(), TAG, "initial sync from fpga failed");

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
