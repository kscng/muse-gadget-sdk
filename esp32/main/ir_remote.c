// StackChan additions to the Muse Gadget SDK, under the same Apache License, Version 2.0.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "ir_pulses.h"
#include "nvs.h"

static const char *TAG = "link.ir";

#define RES_HZ 500000        // 2 us per tick; the longest idle the receiver can time is then 65 ms
#define US_PER_TICK 2
#define RX_SYMBOLS 1024      // capture buffer, in RMT symbols (two pulses each)
#define MAX_PULSES 1200
#define IDLE_END_NS 50000000 // 50 ms of silence ends a capture; air-con remotes pause up to ~40 ms between blocks
#define NS "ir"

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_done;
static rmt_channel_handle_t s_rx, s_tx;
static rmt_encoder_handle_t s_copy;
static rmt_symbol_word_t *s_rx_buf;
static bool s_ready;
static volatile uint32_t s_edges;   // raw changes seen on the receiver pin while learning (diagnostic)

static void IRAM_ATTR on_edge(void *arg) {
    (void)arg;
    s_edges++;
}

static bool on_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *e, void *ctx) {
    (void)ch;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_done, e, &woken);
    return woken == pdTRUE;
}

static esp_err_t init_locked(void) {
    if (s_ready) return ESP_OK;
    if (!s_done) s_done = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (!s_rx_buf) {
        s_rx_buf = heap_caps_aligned_alloc(32, RX_SYMBOLS * sizeof(rmt_symbol_word_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!s_done || !s_rx_buf) return ESP_ERR_NO_MEM;

    rmt_rx_channel_config_t rx = {
        .gpio_num = CONFIG_HOMEHUB_IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = RX_SYMBOLS,
        .flags.with_dma = 1,
    };
    esp_err_t err = rmt_new_rx_channel(&rx, &s_rx);
    if (err != ESP_OK) return err;
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    err = rmt_rx_register_event_callbacks(s_rx, &cbs, NULL);
    if (err == ESP_OK) err = rmt_enable(s_rx);
    if (err != ESP_OK) return err;
    gpio_set_pull_mode(CONFIG_HOMEHUB_IR_RX_GPIO, GPIO_PULLUP_ONLY);

    rmt_tx_channel_config_t tx = {
        .gpio_num = CONFIG_HOMEHUB_IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    rmt_channel_handle_t tx_chan = NULL;
    err = rmt_new_tx_channel(&tx, &tx_chan);
    if (err != ESP_OK) return err;
    s_tx = tx_chan;
    rmt_carrier_config_t carrier = {
        .frequency_hz = CONFIG_HOMEHUB_IR_CARRIER_HZ,
        .duty_cycle = 0.33f,
    };
    rmt_copy_encoder_config_t enc = {};
    err = rmt_apply_carrier(s_tx, &carrier);
    if (err == ESP_OK) err = rmt_new_copy_encoder(&enc, &s_copy);
    if (err == ESP_OK) err = rmt_enable(s_tx);
    if (err != ESP_OK) return err;
    s_ready = true;
    ESP_LOGI(TAG, "ready: LED GPIO%d, receiver GPIO%d, carrier %d Hz", CONFIG_HOMEHUB_IR_TX_GPIO,
             CONFIG_HOMEHUB_IR_RX_GPIO, CONFIG_HOMEHUB_IR_CARRIER_HZ);
    return ESP_OK;
}

static esp_err_t lock_and_init(void) {
    static StaticSemaphore_t buf;
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&buf);   // first use is from the command task; one at a time
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = init_locked();
    if (err != ESP_OK) {   // leave nothing half-made, so the next try starts clean
        if (s_rx) {
            rmt_disable(s_rx);
            rmt_del_channel(s_rx);
        }
        if (s_tx) {
            rmt_disable(s_tx);
            rmt_del_channel(s_tx);
        }
        if (s_copy) rmt_del_encoder(s_copy);
        s_rx = s_tx = NULL;
        s_copy = NULL;
        xSemaphoreGive(s_lock);
    }
    return err;
}

bool ir_remote_name_ok(const char *name) {
    size_t n = name ? strlen(name) : 0;
    if (n < 1 || n > 15) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static esp_err_t store(const char *name, const uint16_t *pulses, size_t n) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, name, pulses, n * sizeof(uint16_t));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t ir_remote_learn(const char *name, int timeout_ms, int *pulses) {
    if (!ir_remote_name_ok(name)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_and_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LEARN FAILED '%s': IR hardware not ready (%s)", name, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "learn '%s': waiting up to %d ms for a button press", name, timeout_ms);

    uint16_t *out = malloc(MAX_PULSES * sizeof(uint16_t));
    ir_half_t *halves = malloc(2 * RX_SYMBOLS * sizeof(ir_half_t));
    if (!out || !halves) {
        free(out);
        free(halves);
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    rmt_receive_config_t cfg = { .signal_range_min_ns = 1250, .signal_range_max_ns = IDLE_END_NS };
    s_edges = 0;
    gpio_install_isr_service(0);   // already installed is fine
    gpio_set_intr_type(CONFIG_HOMEHUB_IR_RX_GPIO, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(CONFIG_HOMEHUB_IR_RX_GPIO, on_edge, NULL);
    int idle_level = gpio_get_level(CONFIG_HOMEHUB_IR_RX_GPIO);
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    err = ESP_ERR_TIMEOUT;
    for (;;) {
        TickType_t left = (int32_t)(end - xTaskGetTickCount()) > 0 ? end - xTaskGetTickCount() : 0;
        if (!left) break;
        xQueueReset(s_done);
        if (rmt_receive(s_rx, s_rx_buf, RX_SYMBOLS * sizeof(rmt_symbol_word_t), &cfg) != ESP_OK) break;
        rmt_rx_done_event_data_t ev;
        if (xQueueReceive(s_done, &ev, left) != pdTRUE) {
            rmt_disable(s_rx);   // give up on this wait: stop the receive that's still armed
            rmt_enable(s_rx);
            break;
        }
        size_t nh = 0;
        for (size_t i = 0; i < ev.num_symbols; i++) {
            rmt_symbol_word_t s = ev.received_symbols[i];
            halves[nh++] = (ir_half_t){ s.duration0, s.level0 };
            halves[nh++] = (ir_half_t){ s.duration1, s.level1 };
        }
        size_t n = ir_pulses_from_halves(halves, nh, US_PER_TICK, out, MAX_PULSES);
        if (n == 0) {   // ambient noise or a button too brief to be one: keep waiting
            ESP_LOGI(TAG, "learn '%s': ignored a short signal (%u symbols)", name, (unsigned)ev.num_symbols);
            continue;
        }
        err = store(name, out, n);
        if (err == ESP_OK) {
            *pulses = (int)n;
            ESP_LOGI(TAG, "LEARN OK '%s': %u pulses saved", name, (unsigned)n);
        } else {
            ESP_LOGW(TAG, "LEARN FAILED '%s': could not save (%s)", name, esp_err_to_name(err));
        }
        break;
    }
    gpio_isr_handler_remove(CONFIG_HOMEHUB_IR_RX_GPIO);
    gpio_set_intr_type(CONFIG_HOMEHUB_IR_RX_GPIO, GPIO_INTR_DISABLE);
    ESP_LOGI(TAG, "receiver pin GPIO%d: %u edges while waiting, level at start %d, now %d", CONFIG_HOMEHUB_IR_RX_GPIO,
             (unsigned)s_edges, idle_level, gpio_get_level(CONFIG_HOMEHUB_IR_RX_GPIO));
    if (err == ESP_ERR_TIMEOUT) ESP_LOGW(TAG, "LEARN FAILED '%s': no signal within %d ms", name, timeout_ms);
    free(out);
    free(halves);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ir_remote_send(const char *name) {
    if (!ir_remote_name_ok(name)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;
    size_t bytes = 0;
    err = nvs_get_blob(h, name, NULL, &bytes);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_ERR_NOT_FOUND;
    uint16_t *pulses = err == ESP_OK && bytes >= 2 ? malloc(bytes) : NULL;
    if (err == ESP_OK && !pulses) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = nvs_get_blob(h, name, pulses, &bytes);
    nvs_close(h);
    if (err != ESP_OK) {
        free(pulses);
        return err;
    }

    size_t n = bytes / sizeof(uint16_t);
    size_t nsym = (n + 1) / 2;
    rmt_symbol_word_t *sym = malloc(nsym * sizeof(*sym));
    if (!sym) {
        free(pulses);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < nsym; i++) {
        uint32_t mark = pulses[2 * i] / US_PER_TICK;
        uint32_t space = 2 * i + 1 < n ? pulses[2 * i + 1] / US_PER_TICK : 1;
        sym[i].level0 = 1;   // carrier on
        sym[i].duration0 = mark < 1 ? 1 : mark > 32767 ? 32767 : mark;
        sym[i].level1 = 0;
        sym[i].duration1 = space < 1 ? 1 : space > 32767 ? 32767 : space;
    }
    free(pulses);

    err = lock_and_init();
    if (err == ESP_OK) {
        rmt_transmit_config_t cfg = {};
        err = rmt_transmit(s_tx, s_copy, sym, nsym * sizeof(*sym), &cfg);
        if (err == ESP_OK) err = rmt_tx_wait_all_done(s_tx, 3000);
        xSemaphoreGive(s_lock);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SEND OK '%s': %u pulses", name, (unsigned)n);
    } else {
        ESP_LOGW(TAG, "SEND FAILED '%s': %s", name, esp_err_to_name(err));
    }
    free(sym);
    return err;
}

int ir_remote_list(char *out, size_t cap) {
    out[0] = 0;
    int count = 0;
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NS, NVS_TYPE_BLOB, &it);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        size_t used = strlen(out);
        if (used + strlen(info.key) + 2 < cap) {
            snprintf(out + used, cap - used, "%s%s", count ? "," : "", info.key);
            count++;
        }
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return count;
}
