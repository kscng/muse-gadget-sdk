// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * StackChan body. Hardware per M5Stack's StackChan-BSP (MIT): two Feetech SCS
 * serial servos on UART1 (TX GPIO6, RX GPIO7, 1 Mbaud), ID 1 yaw and ID 2
 * pitch, powered through pin 0 ("VM EN") of a PY32 I2C expander at 0x6F.
 * One servo step is 0.3125 degrees; the zero positions are per unit.
 * A Si12T (0x68) reads the three-zone touch strip on top of the head. (The 12 RGB
 * LEDs on the same PY32 stayed dark on this unit, so there is no LED code.)
 */
#include "muse_head.h"
#include "muse_head_tone.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "muse_input.h"
#include "muse_state.h"

#define PY32_ADDR 0x6F
#define PY32_REG_VERSION 0x02
#define PY32_REG_DIR_L 0x03   /* 1 = output */
#define PY32_REG_OUT_L 0x05
#define PY32_REG_PU_L 0x09
#define PY32_REG_PD_L 0x0B
#define SERVO_EN_BIT 0x01     /* pin 0 */

#define SERVO_UART UART_NUM_1
#define SERVO_TX 6
#define SERVO_RX 7
#define SERVO_BAUD 1000000
#define YAW_ID 1
#define PITCH_ID 2
#define REG_TORQUE 40
#define REG_GOAL_POS 42
#define RAW_MAX 1000
#define STEPS_PER_DEG 3.2f    /* 1 step = 0.3125 degrees */

/* Conservative limits, degrees from the calibrated home. Yaw + is left, pitch +
 * is up. M5Stack warns the pitch servo stalls outside 5..85 degrees. */
#define YAW_LIMIT 40.0f
#define PITCH_MIN 8.0f
#define PITCH_MAX 40.0f
#define PITCH_REST 12.0f

#define TICK_MS 100
#define MOVE_MS 120           /* each target is reached in this long: smooth at 10 Hz */
#define RELEASE_AFTER_MS 3000 /* torque off once still, as M5Stack's firmware does */

#define TOUCH_ADDR 0x68
#define PROX_ADDR 0x23        /* LTR-553 proximity / ambient light, below the screen */
#define PURR_AFTER_US 1500000 /* a touch held this long is petting, not a tap */
#define BODY_TICK_MS 50

static const char *TAG = "head";
static i2c_master_dev_handle_t s_py32, s_touch, s_prox;
static bool s_prox_ok;             /* the proximity sensor answered with the right part id */
static volatile bool s_purr;       /* being petted: body task writes, head task reads */
static volatile uint8_t s_gesture; /* what the reply's wording asks the head to do, once it starts speaking */

static esp_err_t py32_read(uint8_t reg, uint8_t *v)
{
    return i2c_master_transmit_receive(s_py32, &reg, 1, v, 1, 100);
}

static esp_err_t py32_set_bit(uint8_t reg, uint8_t mask, bool on)
{
    uint8_t v;
    esp_err_t err = py32_read(reg, &v);
    if (err != ESP_OK) {
        return err;
    }
    v = on ? (v | mask) : (v & ~mask);
    uint8_t buf[2] = { reg, v };
    return i2c_master_transmit(s_py32, buf, 2, 100);
}

static void scs_write(uint8_t id, uint8_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t pkt[16] = { 0xFF, 0xFF, id, (uint8_t)(n + 3), 0x03, addr };
    uint8_t sum = id + pkt[3] + 0x03 + addr;
    for (int i = 0; i < n; i++) {
        pkt[6 + i] = data[i];
        sum += data[i];
    }
    pkt[6 + n] = (uint8_t)~sum;
    uart_flush_input(SERVO_UART);   /* the servos' replies aren't used */
    uart_write_bytes(SERVO_UART, pkt, 7 + n);
    uart_wait_tx_done(SERVO_UART, pdMS_TO_TICKS(20));
}

static void torque(bool on)
{
    uint8_t v = on;
    scs_write(YAW_ID, REG_TORQUE, &v, 1);
    scs_write(PITCH_ID, REG_TORQUE, &v, 1);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void servo_move(uint8_t id, int zero, float deg, int ms)
{
    int raw = zero + (int)lroundf(deg * STEPS_PER_DEG);
    raw = raw < 0 ? 0 : raw > RAW_MAX ? RAW_MAX : raw;
    uint8_t d[6] = { (uint8_t)(raw >> 8), (uint8_t)raw, (uint8_t)(ms >> 8), (uint8_t)ms, 0, 0 };
    scs_write(id, REG_GOAL_POS, d, 6);
}


/* ---- Touch strip ---- */

static esp_err_t touch_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_master_transmit(s_touch, b, 2, 100);
}

static esp_err_t touch_read(uint8_t reg, uint8_t *v)
{
    return i2c_master_transmit_receive(s_touch, &reg, 1, v, 1, 100);
}

static void touch_init(void)
{
    /* M5Stack's Si12T setup, with their sensitivity (Type_Low, Level_3 = 0x33) */
    for (uint8_t r = 0x0A; r <= 0x0F; r++) touch_write(r, 0x00);
    touch_write(0x09, 0x0F);
    touch_write(0x09, 0x07);
    touch_write(0x08, 0x22);
    for (uint8_t r = 0x02; r <= 0x06; r++) touch_write(r, 0x33);
}

/* ---- Proximity (hand near the screen) ---- */

static bool prox_read(int *v)
{
    uint8_t reg = 0x8D, d[2];   /* PS_DATA: 11 bits */
    if (!s_prox_ok || i2c_master_transmit_receive(s_prox, &reg, 1, d, 2, 100) != ESP_OK) return false;
    *v = d[0] | ((d[1] & 7) << 8);
    return true;
}

static void prox_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    i2c_master_transmit(s_prox, b, 2, 100);
}

/* The sensor shares the camera's ribbon cable, so it may only answer once the camera is powered: keep trying and
 * log what the part id register says, whenever it changes. */
static void prox_probe(void)
{
    static int last_id = -1, last_man = -1;
    uint8_t reg = 0x86, id = 0xEE, man = 0xEE;
    i2c_master_transmit_receive(s_prox, &reg, 1, &id, 1, 100);
    reg = 0x87;
    i2c_master_transmit_receive(s_prox, &reg, 1, &man, 1, 100);
    if (id != last_id || man != last_man) {
        last_id = id;
        last_man = man;
        ESP_LOGI(TAG, "proximity probe: part id 0x%02x (want 0x92), manufacturer 0x%02x (want 0x05)", id, man);
    }
    static bool powered;
    if (id != 0x92 && !powered) {   /* the camera's power switch (an expander pin) also feeds the sensor */
        powered = true;
        esp_err_t err = bsp_feature_enable(BSP_FEATURE_CAMERA, true);
        ESP_LOGI(TAG, "proximity sensor silent: powering the camera rail (%s)", esp_err_to_name(err));
    }
    if (id == 0x92) {
        prox_write(0x83, 0x0A);   /* PS_N_PULSES: more LED pulses per reading, for more range */
        prox_write(0x81, 0x03);   /* PS_CONTR: active */
        s_prox_ok = true;
        ESP_LOGI(TAG, "proximity sensor on");
    }
}

/* ---- Reading the reply's tone into a head gesture (muse_head_tone.c) ---- */

void muse_head_gesture(const char *text)
{
    s_gesture = text ? muse_head_tone(text) : G_NONE;
    ESP_LOGI(TAG, "reply gesture %u", (unsigned)s_gesture);
}

/* The first moments of speech: tg is seconds since the avatar began speaking. */
static void gesture_pose(uint8_t g, float tg, float *yaw, float *pitch)
{
    *yaw = 0;
    *pitch = 14;
    switch (g) {
    case G_NOD:
        *pitch = 14 - 7 * sinf(tg * 2.0f * (float)M_PI / 0.8f);
        break;
    case G_SHAKE:
        *yaw = 14 * sinf(tg * 2.0f * (float)M_PI / 0.8f) * (1.0f - tg / 1.6f);
        break;
    case G_TILT:
        *yaw = 12 * fminf(1.0f, tg / 0.3f);
        *pitch = 20;
        break;
    case G_BOUNCE:
        *pitch = 14 + 9 * fabsf(sinf(tg * 2.0f * (float)M_PI / 0.7f));
        break;
    default:
        break;
    }
}

/* Petting: polls the touch strip; the head has its own slower task. */
static void body_task(void *arg)
{
    (void)arg;
    bool was_touched = false;
    int64_t touch_since = 0, prox_at_us = 0, prox_log_us = 0, prox_ready_us = 0, prox_probe_us = 0;
    int baseline = -1, near_count = 0;
    vTaskDelay(pdMS_TO_TICKS(5000));   /* the touch strip settles on its baseline first */
    for (;;) {
        uint8_t tv = 0;
        bool touched = s_touch && touch_read(0x10, &tv) == ESP_OK && (tv & 0x3F) != 0;
        int64_t now = esp_timer_get_time();
        if (touched && !was_touched) {
            touch_since = now;
            ESP_LOGI(TAG, "petted (zones %u)", (unsigned)(tv & 0x3F));
            muse_input_wake("pet");
            muse_state_make_happy();
        }
        s_purr = touched && now - touch_since > PURR_AFTER_US;
        was_touched = touched;

        if (s_prox && !s_prox_ok && now - prox_probe_us > 2000000) {   /* not up yet: try again every 2 s */
            prox_probe_us = now;
            prox_probe();
        }

        /* A hand held near the screen wakes the device and starts it listening. */
        int pv;
        if (CONFIG_MUSE_STACKCHAN_PROX_DELTA > 0 && s_prox_ok && now - prox_at_us >= 100000 && prox_read(&pv)) {
            prox_at_us = now;
            if (baseline < 0) baseline = pv;
            int delta = pv - baseline;
            if (delta < 15) {
                baseline += (pv - baseline) / 32;   /* follows the room's own reflection slowly */
            } else if (now - prox_log_us > 400000) {
                prox_log_us = now;
                ESP_LOGI(TAG, "prox %d (room %d, +%d, need +%d)", pv, baseline, delta, CONFIG_MUSE_STACKCHAN_PROX_DELTA);
            }
            near_count = delta >= CONFIG_MUSE_STACKCHAN_PROX_DELTA ? near_count + 1 : 0;
            muse_mode_t m = muse_state_mode(NULL);
            if (near_count >= 3 && now > prox_ready_us && !touched && (m == MUSE_MODE_IDLE || muse_state_asleep())) {
                ESP_LOGI(TAG, "hand near (+%d): listening", delta);
                prox_ready_us = now + 8000000;   /* once, then give the answer time */
                near_count = 0;
                muse_input_talk_until_quiet();
            }
        }
        /* Asleep there's nothing to animate: poll slowly. */
        vTaskDelay(pdMS_TO_TICKS(muse_state_asleep() && !touched ? 4 * BODY_TICK_MS : BODY_TICK_MS));
    }
}

/* The pose for the avatar's current mode; yaw/pitch in degrees from home. */
static void pose(muse_mode_t mode, float t, float level, float *yaw, float *pitch)
{
    static float glance_yaw, glance_pitch = PITCH_REST;
    static int64_t next_glance_us;
    int64_t now = esp_timer_get_time();

    *yaw = 0;
    *pitch = PITCH_REST;
    switch (mode) {
    case MUSE_MODE_THINKING:
        if (t < 20.0f) {   /* a long job: sway for a while, then wait quietly */
            *yaw = 10 * sinf(t * 1.2f);
            *pitch = 15;
        }
        break;
    case MUSE_MODE_SPEAKING:
        *yaw = 4 * sinf(t * 0.9f);
        *pitch = 14 + 5 * sinf(t * 6.0f) * fminf(1.0f, level * 3.0f);
        break;
    case MUSE_MODE_ERROR:
        *yaw = t < 1.5f ? 15 * sinf(t * 14.0f) * (1.5f - t) / 1.5f : 0;
        break;
    case MUSE_MODE_IDLE:
        if (now > next_glance_us) {   /* look somewhere new every 6-12 s */
            glance_yaw = ((int)(esp_random() % 41) - 20);
            glance_pitch = 8 + (esp_random() % 13);
            next_glance_us = now + (6 + esp_random() % 7) * 1000000LL;
        }
        *yaw = glance_yaw;
        *pitch = glance_pitch;
        break;
    default:
        break;
    }
    *yaw = clampf(*yaw, -YAW_LIMIT, YAW_LIMIT);
    *pitch = clampf(*pitch, PITCH_MIN, PITCH_MAX);
}

static void head_task(void *arg)
{
    (void)arg;
    float last_yaw = 1000, last_pitch = 1000;
    int64_t moved_us = 0;
    bool torque_on = false, slept = false;

    vTaskDelay(pdMS_TO_TICKS(3000));   /* let the UI and voice start first */
    for (;;) {
        float secs = 0;
        muse_mode_t mode = muse_state_mode(&secs);
        if (muse_state_asleep() || mode == MUSE_MODE_OFF) {   /* screen off: settle at rest, then go limp */
            if (!slept) {
                slept = true;
                if (!torque_on) {
                    torque(true);
                    torque_on = true;
                }
                servo_move(YAW_ID, CONFIG_MUSE_STACKCHAN_YAW_ZERO, 0, 600);
                servo_move(PITCH_ID, CONFIG_MUSE_STACKCHAN_PITCH_ZERO, PITCH_REST, 600);
                moved_us = esp_timer_get_time();
            } else if (torque_on && esp_timer_get_time() - moved_us > 1500 * 1000LL) {
                torque(false);
                torque_on = false;
            }
            last_yaw = last_pitch = 1000;   /* move again on waking */
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
            continue;
        }
        slept = false;
        if (mode == MUSE_MODE_LISTENING) {   /* PWR is being held: don't move under the finger */
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
            continue;
        }
        float yaw, pitch;
        float happy = muse_state_happiness();   /* the hearts: wiggle with the avatar's hop */
        uint8_t gesture = s_gesture;
        if (mode == MUSE_MODE_IDLE) {
            s_gesture = G_NONE;   /* the reply was never spoken: forget its gesture */
        } else if (mode == MUSE_MODE_SPEAKING && secs >= 1.6f) {
            s_gesture = G_NONE;   /* spent; the head goes back to nodding along */
        }
        if (happy > 0) {
            float tt = esp_timer_get_time() / 1e6f;
            yaw = 8 * sinf(tt * 7.0f) * happy;
            pitch = PITCH_REST + 6 * fabsf(sinf(tt * 9.0f)) * happy;
        } else if (s_purr && mode == MUSE_MODE_IDLE) {   /* being petted: a slow contented sway */
            float tt = esp_timer_get_time() / 1e6f;
            yaw = 3 * sinf(tt * 0.9f);
            pitch = 10 + 2 * sinf(tt * 0.6f);
        } else if (gesture && mode == MUSE_MODE_SPEAKING && secs < 1.6f) {
            gesture_pose(gesture, secs, &yaw, &pitch);
        } else {
            pose(mode, secs, muse_state_level(), &yaw, &pitch);
        }

        if (fabsf(yaw - last_yaw) > 0.5f || fabsf(pitch - last_pitch) > 0.5f) {
            if (!torque_on) {
                torque(true);
                torque_on = true;
            }
            servo_move(YAW_ID, CONFIG_MUSE_STACKCHAN_YAW_ZERO, yaw, MOVE_MS);
            servo_move(PITCH_ID, CONFIG_MUSE_STACKCHAN_PITCH_ZERO, pitch, MOVE_MS);
            last_yaw = yaw;
            last_pitch = pitch;
            moved_us = esp_timer_get_time();
        } else if (torque_on && esp_timer_get_time() - moved_us > RELEASE_AFTER_MS * 1000LL) {
            torque(false);
            torque_on = false;
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t muse_head_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PY32_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_py32);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no I2C device: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t ver = 0;
    if (py32_read(PY32_REG_VERSION, &ver) != ESP_OK || ver == 0 || ver == 0xFF) {
        ESP_LOGW(TAG, "servo power chip not found (not a StackChan body?)");
        return ESP_ERR_NOT_FOUND;
    }
    /* VM EN: output, pull-up, high. */
    err = py32_set_bit(PY32_REG_DIR_L, SERVO_EN_BIT, true);
    if (err == ESP_OK) err = py32_set_bit(PY32_REG_PD_L, SERVO_EN_BIT, false);
    if (err == ESP_OK) err = py32_set_bit(PY32_REG_PU_L, SERVO_EN_BIT, true);
    if (err == ESP_OK) err = py32_set_bit(PY32_REG_OUT_L, SERVO_EN_BIT, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "servo power on failed: %s", esp_err_to_name(err));
        return err;
    }

    uart_config_t uc = {
        .baud_rate = SERVO_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_driver_install(SERVO_UART, 256, 256, 0, NULL, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_param_config(SERVO_UART, &uc));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_pin(SERVO_UART, SERVO_TX, SERVO_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "servos powered (PY32 v%u), yaw zero %d, pitch zero %d", ver, CONFIG_MUSE_STACKCHAN_YAW_ZERO,
             CONFIG_MUSE_STACKCHAN_PITCH_ZERO);
    xTaskCreate(head_task, "muse_head", 3072, NULL, 3, NULL);

    i2c_device_config_t tcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &tcfg, &s_touch) == ESP_OK) {
        touch_init();
    } else {
        s_touch = NULL;
        ESP_LOGW(TAG, "no touch strip: petting is off");
    }
    i2c_device_config_t pcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PROX_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &pcfg, &s_prox) != ESP_OK) {
        s_prox = NULL;
        ESP_LOGW(TAG, "no proximity sensor device");
    } else {
        prox_probe();
    }
    xTaskCreate(body_task, "muse_body", 3072, NULL, 3, NULL);
    return ESP_OK;
}
