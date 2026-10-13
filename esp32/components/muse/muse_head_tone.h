// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/* A reply's tone as a head gesture. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { G_NONE, G_NOD, G_SHAKE, G_TILT, G_BOUNCE, G_DROOP };

/* A question tilts, a refusal shakes, agreement nods, cheer bounces; anything else is G_NONE. */
uint8_t muse_head_tone(const char *text);

/*
 * Emotions Muse can ask for with a tag at the very start of a reply, e.g. "[excited] Great news!". The names are
 * ElevenLabs audio tags. Index 0 is "no emotion".
 */
#define MUSE_EMOTIONS 16

const char *muse_emotion_name(int idx);   /* "excited", ...; "" for 0 */
uint8_t muse_emotion_gesture(int idx);    /* the head gesture that goes with it */

/*
 * Looks for a known tag at the start of `s`. Returns its index and sets *used to the bytes to drop (the tag and the
 * spaces after it); returns -1 when `s` could still turn into a tag as more text arrives; 0 when it is not one
 * (an unknown tag such as "[1]" is not one, and is left in the text).
 */
int muse_emotion_parse(const char *s, size_t *used);

#ifdef __cplusplus
}
#endif
