// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * Network trigger for the talk button (CONFIG_MUSE_REMOTE): see muse_remote.c.
 */
#pragma once

/* Starts the HTTP listener once Wi-Fi has an address. Off without a token. */
void muse_remote_start(void);
