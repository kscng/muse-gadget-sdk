// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/* A reply's tone as a head gesture. */
#pragma once

#include <stdint.h>

enum { G_NONE, G_NOD, G_SHAKE, G_TILT, G_BOUNCE };

/* A question tilts, a refusal shakes, agreement nods, cheer bounces; anything else is G_NONE. */
uint8_t muse_head_tone(const char *text);
