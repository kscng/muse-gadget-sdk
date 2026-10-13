// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * One multipart upload to ElevenLabs' speech-to-text endpoint: the whole recording as a WAV, language left to
 * its detection (Cantonese, English and a mix of them are all expected).
 */
#include "muse_stt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "muse_audio.h"

static const char *TAG = "stt";
static const char KEY[] = CONFIG_MUSE_ELEVENLABS_API_KEY;

#define BOUNDARY "----stackchan-7f3a9c"
#define MODEL "scribe_v2"
#define REPLY_MAX 6144

int muse_stt_enabled(void)
{
    return sizeof(KEY) > 1;
}

static void wav_header(uint8_t *h, uint32_t pcm_bytes)
{
    const uint32_t rate = MUSE_AUDIO_RATE, byte_rate = MUSE_AUDIO_RATE * 2, riff = 36 + pcm_bytes;
    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    const uint32_t fmt_len = 16;
    memcpy(h + 16, &fmt_len, 4);
    const uint16_t pcm_fmt = 1, channels = 1, block = 2, bits = 16;
    memcpy(h + 20, &pcm_fmt, 2);
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &block, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &pcm_bytes, 4);
}

static bool write_all(esp_http_client_handle_t h, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        size_t n = len > 4096 ? 4096 : len;
        int w = esp_http_client_write(h, p, (int)n);
        if (w <= 0) return false;
        p += w;
        len -= (size_t)w;
    }
    return true;
}

/* One try. *retry: another attempt could help (busy, or the connection dropped). */
static char *attempt(const int16_t *pcm, size_t frames, bool *retry)
{
    *retry = false;
    char fields[640];
    int fl = snprintf(fields, sizeof(fields),
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"model_id\"\r\n\r\n" MODEL "\r\n"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"tag_audio_events\"\r\n\r\nfalse\r\n"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"timestamps_granularity\"\r\n\r\nnone\r\n"
        "%s"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        sizeof(CONFIG_MUSE_SCRIBE_LANGUAGE) > 1
            ? "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"language_code\"\r\n\r\n" CONFIG_MUSE_SCRIBE_LANGUAGE "\r\n"
            : "");
    if (fl <= 0 || fl >= (int)sizeof(fields)) return NULL;
    static const char tail[] = "\r\n--" BOUNDARY "--\r\n";
    uint8_t wav[44];
    size_t pcm_bytes = frames * sizeof(int16_t);
    wav_header(wav, (uint32_t)pcm_bytes);

    esp_http_client_config_t cfg = {};
    cfg.url = "https://api.elevenlabs.io/v1/speech-to-text";
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 20000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return NULL;
    esp_http_client_set_header(h, "xi-api-key", KEY);
    esp_http_client_set_header(h, "Content-Type", "multipart/form-data; boundary=" BOUNDARY);

    char *text = NULL;
    size_t total = (size_t)fl + sizeof(wav) + pcm_bytes + sizeof(tail) - 1;
    if (esp_http_client_open(h, (int)total) == ESP_OK && write_all(h, fields, (size_t)fl) &&
        write_all(h, wav, sizeof(wav)) && write_all(h, pcm, pcm_bytes) && write_all(h, tail, sizeof(tail) - 1) &&
        esp_http_client_fetch_headers(h) >= 0) {
        int status = esp_http_client_get_status_code(h);
        char *body = heap_caps_malloc(REPLY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        int n = 0;
        while (body) {
            int r = esp_http_client_read(h, body + n, REPLY_MAX - 1 - n);
            if (r <= 0) break;
            n += r;
        }
        if (body) body[n] = 0;
        if (status == 200 && body) {
            cJSON *root = cJSON_Parse(body);
            const cJSON *t = root ? cJSON_GetObjectItem(root, "text") : NULL;
            if (cJSON_IsString(t) && t->valuestring) {
                const char *s = t->valuestring;
                while (*s == ' ') s++;
                text = strdup(s);
                const cJSON *lang = cJSON_GetObjectItem(root, "language_code");
                const cJSON *prob = cJSON_GetObjectItem(root, "language_probability");
                ESP_LOGI(TAG, "heard (%s %.2f): \"%s\"", cJSON_IsString(lang) ? lang->valuestring : "?",
                         cJSON_IsNumber(prob) ? prob->valuedouble : 0.0, text ? text : "");
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "HTTP %d: %.120s", status, body ? body : "");
            *retry = status == 429 || status >= 500;
        }
        free(body);
    } else {
        ESP_LOGW(TAG, "request failed");
        *retry = true;
    }
    esp_http_client_cleanup(h);
    return text;
}

char *muse_stt_transcribe(const int16_t *pcm, size_t frames)
{
    if (!muse_stt_enabled() || !pcm || !frames) return NULL;
    char *text = NULL;
    bool retry = false;
    for (int tries = 1; tries <= 2; tries++) {   /* the voice turn is waiting: one retry only */
        text = attempt(pcm, frames, &retry);
        if (text || !retry) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return text;
}
