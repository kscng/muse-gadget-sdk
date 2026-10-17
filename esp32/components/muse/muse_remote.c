// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * Network trigger for the talk button, so the StackChan can be started from a
 * computer on the same Wi-Fi with no USB cable:
 *   POST /talk          listen until you stop talking (about 1.4 s of quiet), like holding the button
 *   POST /talk?secs=8   listen for a fixed 8 s (1-15) instead
 *   POST /talk/down     the button goes down      POST /talk/up   it comes up
 *   GET  /photo         one JPEG photo from the camera (only when asked)
 *   GET  /status        the avatar's mode and whether the screen is asleep
 * Every request needs an "X-Token" header equal to CONFIG_MUSE_REMOTE_TOKEN.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "muse_input.h"
#if CONFIG_MUSE_CORES3_CAMERA
#include "muse_camera.h"
#endif
#include "muse_state.h"

static const char *TAG = "remote";
static const char TOKEN[] = CONFIG_MUSE_REMOTE_TOKEN;
static volatile bool s_talking;

static bool authed(httpd_req_t *req)
{
    char given[96] = "";
    if (httpd_req_get_hdr_value_str(req, "X-Token", given, sizeof(given)) != ESP_OK) {
        return false;
    }
    size_t n = strlen(TOKEN);
    unsigned diff = strlen(given) ^ (unsigned)n;
    for (size_t i = 0; i < n; i++) {   /* no early exit: the time doesn't say how much matched */
        diff |= (unsigned char)given[i < sizeof(given) ? i : 0] ^ (unsigned char)TOKEN[i];
    }
    return diff == 0;
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static void talk_task(void *arg)
{
    int secs = (int)(intptr_t)arg;
    muse_input_remote_talk(true);
    vTaskDelay(pdMS_TO_TICKS(secs * 1000));
    muse_input_remote_talk(false);
    s_talking = false;
    vTaskDelete(NULL);
}

static esp_err_t talk_handler(httpd_req_t *req)
{
    if (!authed(req)) return reply(req, "401 Unauthorized", "{\"ok\":false,\"error\":\"bad token\"}");
    int secs = 0;   // 0 = until you stop talking
    char q[32], v[8];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "secs", v, sizeof(v)) == ESP_OK && strcmp(v, "auto") != 0) {
        secs = atoi(v);
        if (secs < 1 || secs > 15) return reply(req, "400 Bad Request", "{\"ok\":false,\"error\":\"secs must be 1-15 or auto\"}");
    }
    if (secs == 0) {   /* until you stop talking */
        if (!muse_input_talk_until_quiet()) return reply(req, "409 Conflict", "{\"ok\":false,\"error\":\"already listening\"}");
        ESP_LOGI(TAG, "talk until quiet");
        return reply(req, "200 OK", "{\"ok\":true}");
    }
    if (s_talking) return reply(req, "409 Conflict", "{\"ok\":false,\"error\":\"already listening\"}");
    s_talking = true;
    if (xTaskCreate(talk_task, "remote_talk", 3072, (void *)(intptr_t)secs, 5, NULL) != pdPASS) {
        s_talking = false;
        return reply(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"no memory\"}");
    }
    ESP_LOGI(TAG, "talk for %d s", secs);
    return reply(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t down_up_handler(httpd_req_t *req)
{
    if (!authed(req)) return reply(req, "401 Unauthorized", "{\"ok\":false,\"error\":\"bad token\"}");
    bool down = strstr(req->uri, "down") != NULL;
    ESP_LOGI(TAG, "talk %s", down ? "down" : "up");
    muse_input_remote_talk(down);
    return reply(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t photo_handler(httpd_req_t *req)
{
    if (!authed(req)) return reply(req, "401 Unauthorized", "{\"ok\":false,\"error\":\"bad token\"}");
#if CONFIG_MUSE_CORES3_CAMERA
    size_t len = 0;
    const char *why = "camera failed";
    uint8_t *jpeg = muse_camera_capture_jpeg(&len, &why);
    if (!jpeg) {
        char body[96];
        snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", why);
        return reply(req, "503 Service Unavailable", body);
    }
    ESP_LOGI(TAG, "photo requested: %u bytes", (unsigned)len);
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t r = httpd_resp_send(req, (const char *)jpeg, len);
    free(jpeg);
    return r;
#else
    return reply(req, "501 Not Implemented", "{\"ok\":false,\"error\":\"no camera\"}");
#endif
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authed(req)) return reply(req, "401 Unauthorized", "{\"ok\":false,\"error\":\"bad token\"}");
    static const char *const names[MUSE_MODE_COUNT] = { "boot", "idle", "listening", "thinking", "speaking", "error", "off" };
    muse_mode_t m = muse_state_mode(NULL);
    char body[96];
    snprintf(body, sizeof(body), "{\"ok\":true,\"mode\":\"%s\",\"asleep\":%s}", m < MUSE_MODE_COUNT ? names[m] : "?",
             muse_state_asleep() ? "true" : "false");
    return reply(req, "200 OK", body);
}

static void start_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_MUSE_REMOTE_PORT;
    cfg.max_open_sockets = 3;
    cfg.stack_size = 8192;   /* the photo handler runs the camera and JPEG encoder */
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;   /* its stack in PSRAM: internal RAM is scarce */
    httpd_handle_t srv;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "could not start the server");
        return;
    }
    static const httpd_uri_t uris[] = {
        { .uri = "/talk", .method = HTTP_POST, .handler = talk_handler },
        { .uri = "/talk/down", .method = HTTP_POST, .handler = down_up_handler },
        { .uri = "/talk/up", .method = HTTP_POST, .handler = down_up_handler },
        { .uri = "/photo", .method = HTTP_GET, .handler = photo_handler },
        { .uri = "/status", .method = HTTP_GET, .handler = status_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(srv, &uris[i]);
    }
}

/* Waits for Wi-Fi to have an address, then serves; the server keeps running across reconnects. */
static void remote_task(void *arg)
{
    (void)arg;
    esp_netif_ip_info_t ip = {};
    for (;;) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr) break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    start_server();
    ESP_LOGI(TAG, "listening on http://" IPSTR ":%d", IP2STR(&ip.ip), CONFIG_MUSE_REMOTE_PORT);
    vTaskDelete(NULL);
}

void muse_remote_start(void)
{
    if (sizeof(TOKEN) <= 1) {
        ESP_LOGW(TAG, "no token set: network trigger is off");
        return;
    }
    xTaskCreate(remote_task, "muse_remote", 3072, NULL, 3, NULL);
}
