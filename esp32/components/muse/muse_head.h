// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * StackChan head: moves the two servos with the avatar state (listening looks
 * up, thinking sways, speaking nods with the voice). CoreS3 on a StackChan
 * body only, CONFIG_MUSE_STACKCHAN_HEAD.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Powers the servos (PY32 expander on `bus`) and starts the motion task. */
esp_err_t muse_head_init(i2c_master_bus_handle_t bus);

/* Picks the head gesture for a reply from its first sentence (a nod for yes, a shake for no, a tilt for a
 * question, a bounce for cheer) and plays it when the avatar starts speaking. */
void muse_head_gesture(const char *text);

/* The same, for a reply Muse tagged with an emotion (index as in muse_head_tone.h). */
void muse_head_gesture_emotion(int emotion);

#ifdef __cplusplus
}
#endif
