// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * Takes one photo through the board's camera driver (esp_video, V4L2 style) and JPEG-encodes it in software;
 * the S3 has no JPEG hardware. The sensor is only streaming while a photo is being taken.
 */
#include "muse_camera.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "esp_video_device.h"
#include "muse_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#define BUFS 2
#define SKIP_FRAMES 8        /* the sensor's auto-exposure needs a few frames to settle (about half a second) */
#define FRAME_TIMEOUT_MS 3000
#define JPEG_QUALITY 60
#define JPEG_MAX_BYTES (200 * 1024)

static atomic_bool s_busy;
static bool s_started;

static bool jpeg_format(uint32_t v4l2, jpeg_pixel_format_t *out, jpeg_subsampling_t *sub)
{
    *sub = JPEG_SUBSAMPLE_420;
    switch (v4l2) {
    case V4L2_PIX_FMT_RGB565:  *out = JPEG_PIXEL_FORMAT_RGB565_LE; return true;
    case V4L2_PIX_FMT_RGB565X: *out = JPEG_PIXEL_FORMAT_RGB565_BE; return true;
    case V4L2_PIX_FMT_YUYV:    *out = JPEG_PIXEL_FORMAT_YCbYCr; return true;
    case V4L2_PIX_FMT_UYVY:    *out = JPEG_PIXEL_FORMAT_CbYCrY; return true;
    case V4L2_PIX_FMT_GREY:    *out = JPEG_PIXEL_FORMAT_GRAY; *sub = JPEG_SUBSAMPLE_GRAY; return true;
    default: return false;
    }
}

static uint8_t *capture(size_t *len, const char **why)
{
    uint8_t *jpeg = NULL;
    void *maps[BUFS] = {};
    size_t map_len[BUFS] = {};
    bool streaming = false;
    int fd = -1;

    muse_audio_camera_cue(0);   /* "about to shoot" */
    if (!s_started) {
        esp_err_t err = bsp_camera_start(NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "camera start failed: %s", esp_err_to_name(err));
            *why = "the camera didn't start";
            return NULL;
        }
        s_started = true;
    }
    fd = open(BSP_CAMERA_DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        ESP_LOGW(TAG, "open %s failed: errno %d", BSP_CAMERA_DEVICE, errno);
        *why = "couldn't open the camera";
        return NULL;
    }

    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) != 0) {
        *why = "couldn't read the camera format";
        goto done;
    }
    int w = fmt.fmt.pix.width, h = fmt.fmt.pix.height;
    jpeg_pixel_format_t src;
    jpeg_subsampling_t sub;
    if (!jpeg_format(fmt.fmt.pix.pixelformat, &src, &sub)) {
        ESP_LOGW(TAG, "unsupported camera format 0x%08x", (unsigned)fmt.fmt.pix.pixelformat);
        *why = "unsupported camera format";
        goto done;
    }

    struct v4l2_requestbuffers req = { .count = BUFS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        *why = "camera buffers failed";
        goto done;
    }
    for (int i = 0; i < BUFS; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0) {
            *why = "camera buffers failed";
            goto done;
        }
        maps[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        map_len[i] = b.length;
        if (maps[i] == MAP_FAILED) {
            maps[i] = NULL;
            *why = "camera buffers failed";
            goto done;
        }
        if (ioctl(fd, VIDIOC_QBUF, &b) != 0) {
            *why = "camera buffers failed";
            goto done;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        *why = "camera wouldn't start";
        goto done;
    }
    streaming = true;

    struct v4l2_buffer frame = {};
    int64_t t_stream = esp_timer_get_time();
    for (int n = 0; n <= SKIP_FRAMES; n++) {
        if (n == SKIP_FRAMES) {
            muse_audio_camera_cue(1);   /* the click, as the picture is taken */
        }
        int64_t t0 = esp_timer_get_time();
        for (;;) {   /* non-blocking: EAGAIN until the next frame is ready */
            memset(&frame, 0, sizeof(frame));
            frame.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            frame.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &frame) == 0) break;
            if (errno != EAGAIN || (esp_timer_get_time() - t0) / 1000 > FRAME_TIMEOUT_MS) {
                ESP_LOGW(TAG, "no frame (errno %d)", errno);
                *why = "the camera gave no picture";
                goto done;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        ESP_LOGI(TAG, "frame %d after %lld ms (%lld ms since streaming)", n, (long long)((esp_timer_get_time() - t0) / 1000),
                 (long long)((esp_timer_get_time() - t_stream) / 1000));
        if (n < SKIP_FRAMES) {
            ioctl(fd, VIDIOC_QBUF, &frame);   /* discard; keep the sensor running */
        }
    }

    /* The frame is held (not re-queued) while it's encoded. */
    jpeg = heap_caps_aligned_alloc(16, JPEG_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg) {
        *why = "out of memory for the photo";
        goto done;
    }
    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = w;
    cfg.height = h;
    cfg.src_type = src;
    cfg.subsampling = sub;
    cfg.quality = JPEG_QUALITY;
    cfg.rotate = JPEG_ROTATE_0D;
    cfg.task_enable = false;
    jpeg_enc_handle_t enc = NULL;
    int out_len = 0;
    int64_t t_enc = esp_timer_get_time();
    jpeg_error_t je = jpeg_enc_open(&cfg, &enc);
    if (je == JPEG_ERR_OK) {
        je = jpeg_enc_process(enc, maps[frame.index], frame.bytesused ? frame.bytesused : map_len[frame.index], jpeg,
                              JPEG_MAX_BYTES, &out_len);
        jpeg_enc_close(enc);
    }
    if (je != JPEG_ERR_OK || out_len <= 0) {
        ESP_LOGW(TAG, "JPEG encode failed (%d)", (int)je);
        free(jpeg);
        jpeg = NULL;
        *why = "couldn't encode the photo";
        goto done;
    }
    *len = (size_t)out_len;
    ESP_LOGI(TAG, "photo %dx%d, format 0x%08x, %d bytes, encoded in %lld ms", w, h, (unsigned)fmt.fmt.pix.pixelformat, out_len,
             (long long)((esp_timer_get_time() - t_enc) / 1000));

done:
    if (streaming) {
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < BUFS; i++) {
        if (maps[i]) munmap(maps[i], map_len[i]);
    }
    if (fd >= 0) close(fd);
    return jpeg;
}

uint8_t *muse_camera_capture_jpeg(size_t *len, const char **why)
{
    static const char *dummy;
    if (!why) why = &dummy;
    *len = 0;
    if (atomic_exchange(&s_busy, true)) {
        *why = "camera busy";
        return NULL;
    }
    uint8_t *jpeg = capture(len, why);
    atomic_store(&s_busy, false);
    return jpeg;
}
