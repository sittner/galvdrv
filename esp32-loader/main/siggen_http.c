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
// Scope UI
"<div class='card' style='margin-top:1rem'><h2>Scope</h2>"
"<div style='display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin-bottom:.5rem'>"
"<select id='sc_tb'><option value='0'>500&micro;s</option><option value='1' selected>1ms</option><option value='2'>2ms</option><option value='3'>5ms</option>"
"<option value='4'>10ms</option><option value='5'>20ms</option><option value='6'>50ms</option><option value='7'>100ms</option></select>"
"<select id='sc_tch'><option value='0'>Trig:Ch1</option><option value='1'>Trig:Ch2</option><option value='2'>Trig:Ch3</option><option value='3'>Trig:Ch4</option></select>"
"<select id='sc_te'><option value='rising'>Rising</option><option value='falling'>Falling</option></select>"
"<select id='sc_tm'><option value='single'>Single</option><option value='normal'>Normal</option><option value='manual'>Manual</option></select>"
"<button id='sc_arm'>Arm</button><button id='sc_force'>Force</button>"
"<button id='sc_csv'>CSV</button>"
"<span id='sc_st' class='hint' style='margin-left:.5rem'>Idle</span>"
"</div>"
"<canvas id='sc_cv' width='1000' height='400' style='width:100%;border:1px solid #ccd;border-radius:8px;background:#1a1a2e'></canvas>"
"<div class='hint' style='margin-top:.3rem'>"
"<span style='color:#0f0'>&#9632;Ch1</span> "
"<span style='color:#ff0'>&#9632;Ch2</span> "
"<span style='color:#0ff'>&#9632;Ch3</span> "
"<span style='color:#f0f'>&#9632;Ch4</span></div></div>"
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
"bindCard(0);bindCard(1);load().catch(e=>{el('status').textContent='Error: '+e.message;});"
// Scope JS
"const cv=el('sc_cv'),ctx=cv.getContext('2d');"
"const COLORS=['#00ff00','#ffff00','#00ffff','#ff00ff'];"
"const DIVX=10,DIVY=8,SPD=128,NSAMPLES=DIVX*SPD;"
"let scopeData=null,scopePoll=null;"
"function drawGrid(){ctx.fillStyle='#1a1a2e';ctx.fillRect(0,0,cv.width,cv.height);"
"ctx.strokeStyle='#334';ctx.lineWidth=1;"
"for(let i=0;i<=DIVX;i++){let x=i*cv.width/DIVX;ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,cv.height);ctx.stroke();}"
"for(let i=0;i<=DIVY;i++){let y=i*cv.height/DIVY;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(cv.width,y);ctx.stroke();}}"
"function drawTraces(){if(!scopeData)return;drawGrid();"
"for(let ch=0;ch<4;ch++){const d=scopeData[ch];if(!d)continue;"
"ctx.strokeStyle=COLORS[ch];ctx.lineWidth=1.5;ctx.beginPath();"
"for(let i=0;i<d.length;i++){let x=i*cv.width/NSAMPLES;let y=cv.height-(d[i]/4095)*cv.height;"
"if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}ctx.stroke();}}"
"async function scopeArm(){const p={command:'arm',trig_ch:+el('sc_tch').value,trig_edge:el('sc_te').value,trig_mode:el('sc_tm').value,timebase:+el('sc_tb').value};"
"await fetch('/api/scope',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});"
"el('sc_st').textContent='Armed';startPoll();}"
"async function scopeForce(){await fetch('/api/scope',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command:'force'})});}"
"async function pollScope(){const r=await fetch('/api/scope');const j=await r.json();"
"const states=['Idle','Armed','Triggered','Done'];el('sc_st').textContent=states[j.state]||'?';"
"if(j.state===3&&j.channels){scopeData=j.channels;drawTraces();stopPoll();"
"if(el('sc_tm').value==='normal'){setTimeout(scopeArm,50);}}}"
"function startPoll(){stopPoll();scopePoll=setInterval(pollScope,200);}"
"function stopPoll(){if(scopePoll){clearInterval(scopePoll);scopePoll=null;}}"
"function exportCSV(){if(!scopeData)return;let csv='Sample,Ch1,Ch2,Ch3,Ch4\\n';"
"for(let i=0;i<NSAMPLES;i++){csv+=i+','+scopeData.map(ch=>(ch[i]||0)).join(',')+String.fromCharCode(10);}"
"const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));a.download='scope.csv';a.click();}"
"el('sc_arm').onclick=scopeArm;el('sc_force').onclick=scopeForce;el('sc_csv').onclick=exportCSV;"
"drawGrid();</script></body></html>";

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

esp_err_t siggen_push_state_to_fpga(void)
{
    for (uint8_t ch = 0; ch < 2; ++ch) {
        esp_err_t err = apply_channel_state(ch);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "push ch%u to FPGA failed: %s", ch, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "state pushed to FPGA");
    return ESP_OK;
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

// --- Scope Handlers ---

// Timebase presets: sample_div values for 32 MHz clock
// sample_rate = 32MHz / (sample_div + 1), time_per_div = 128 / sample_rate
static const struct {
    const char *label;
    uint16_t sample_div;
} s_timebases[] = {
    {"500us",  124},   // 256 kHz -> 500us/div
    {"1ms",    249},   // 128 kHz -> 1ms/div
    {"2ms",    499},   // 64 kHz  -> 2ms/div
    {"5ms",    1249},  // 25.6 kHz -> 5ms/div
    {"10ms",   2499},  // 12.8 kHz -> 10ms/div
    {"20ms",   4999},  // 6.4 kHz -> 20ms/div
    {"50ms",   12499}, // 2.56 kHz -> 50ms/div
    {"100ms",  24999}, // 1.28 kHz -> 100ms/div
};
#define NUM_TIMEBASES (sizeof(s_timebases) / sizeof(s_timebases[0]))

static esp_err_t scope_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    uint8_t state = 0;
    uint16_t trig_ptr = 0;
    esp_err_t err = scope_get_status(&state, &trig_ptr);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scope status read failed");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "state", state);
    cJSON_AddNumberToObject(root, "trig_ptr", trig_ptr);

    // If done, read the full buffer
    if (state == SCOPE_ST_DONE) {
        uint16_t *buf = malloc(SCOPE_SAMPLES_PER_CH * SCOPE_NUM_CHANNELS * sizeof(uint16_t));
        if (!buf) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        }
        err = scope_read_buffer(0, buf, SCOPE_SAMPLES_PER_CH * SCOPE_NUM_CHANNELS);
        if (err != ESP_OK) {
            free(buf);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scope buffer read failed");
        }

        // Interleaved: [ch0_s0, ch1_s0, ch2_s0, ch3_s0, ch0_s1, ...]
        // Rearrange to per-channel arrays
        cJSON *channels = cJSON_AddArrayToObject(root, "channels");
        for (int ch = 0; ch < SCOPE_NUM_CHANNELS; ++ch) {
            cJSON *arr = cJSON_CreateArray();
            for (int s = 0; s < SCOPE_SAMPLES_PER_CH; ++s) {
                cJSON_AddItemToArray(arr, cJSON_CreateNumber(buf[s * SCOPE_NUM_CHANNELS + ch]));
            }
            cJSON_AddItemToArray(channels, arr);
        }
        free(buf);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
    }

    err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t scope_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (req->content_len <= 0 || req->content_len > 512) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }

    char *body = calloc((size_t)req->content_len + 1, 1);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
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

    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "command");
    if (cJSON_IsString(cmd)) {
        if (strcmp(cmd->valuestring, "arm") == 0) {
            // Configure and arm
            const cJSON *trig_ch_item = cJSON_GetObjectItemCaseSensitive(root, "trig_ch");
            const cJSON *trig_edge_item = cJSON_GetObjectItemCaseSensitive(root, "trig_edge");
            const cJSON *trig_mode_item = cJSON_GetObjectItemCaseSensitive(root, "trig_mode");
            const cJSON *timebase_item = cJSON_GetObjectItemCaseSensitive(root, "timebase");

            uint8_t trig_ch = cJSON_IsNumber(trig_ch_item) ? (uint8_t)trig_ch_item->valueint : 0;
            bool trig_falling = cJSON_IsString(trig_edge_item) && strcmp(trig_edge_item->valuestring, "falling") == 0;
            uint8_t trig_mode = SCOPE_TRIG_SINGLE;
            if (cJSON_IsString(trig_mode_item)) {
                if (strcmp(trig_mode_item->valuestring, "normal") == 0) trig_mode = SCOPE_TRIG_NORMAL;
                else if (strcmp(trig_mode_item->valuestring, "manual") == 0) trig_mode = SCOPE_TRIG_MANUAL;
            }

            uint16_t sample_div = 249; // default 1ms/div
            if (cJSON_IsNumber(timebase_item)) {
                int tb_idx = timebase_item->valueint;
                if (tb_idx >= 0 && tb_idx < (int)NUM_TIMEBASES) {
                    sample_div = s_timebases[tb_idx].sample_div;
                }
            }

            esp_err_t err = scope_configure(trig_ch, trig_falling, trig_mode, sample_div);
            if (err == ESP_OK) err = scope_arm();
            cJSON_Delete(root);
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scope arm failed");
            }
            return httpd_resp_sendstr(req, "{\"ok\":true}");

        } else if (strcmp(cmd->valuestring, "force") == 0) {
            cJSON_Delete(root);
            esp_err_t err = scope_force_trigger();
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scope force failed");
            }
            return httpd_resp_sendstr(req, "{\"ok\":true}");
        }
    }

    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid command");
}

esp_err_t siggen_http_register(httpd_handle_t server)
{
    siggen_push_state_to_fpga();

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

    httpd_uri_t scope_get_uri = {
        .uri = "/api/scope",
        .method = HTTP_GET,
        .handler = scope_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t scope_post_uri = {
        .uri = "/api/scope",
        .method = HTTP_POST,
        .handler = scope_post_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &scope_get_uri), TAG, "register GET /api/scope failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &scope_post_uri), TAG, "register POST /api/scope failed");

    return ESP_OK;
}
