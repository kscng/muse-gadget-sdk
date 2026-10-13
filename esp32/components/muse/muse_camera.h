// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * One still photo from the CoreS3's camera (GC0308), only when asked: nothing streams and nothing is kept.
 * CONFIG_MUSE_CORES3_CAMERA.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A 640x480 JPEG, malloc'd (free() it). NULL with *why set on failure. */
uint8_t *muse_camera_capture_jpeg(size_t *len, const char **why);

#ifdef __cplusplus
}
#endif
