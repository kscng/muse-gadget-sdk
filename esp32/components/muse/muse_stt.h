// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * Speech to text with ElevenLabs Scribe, for what the StackChan heard (CONFIG_MUSE_SCRIBE_STT).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether an ElevenLabs key is set. */
int muse_stt_enabled(void);

/* Transcribes 16-bit mono PCM at MUSE_AUDIO_RATE. Returns the text, malloc'd: empty when the service answered but
 * heard no speech. NULL when it couldn't be reached or refused, so the caller can send the audio to Muse instead. */
char *muse_stt_transcribe(const int16_t *pcm, size_t frames);

#ifdef __cplusplus
}
#endif
