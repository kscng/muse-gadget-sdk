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

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Learn remote-control buttons with the StackChan's IR receiver and play them
// back with its IR LED. A button is stored as raw mark/space timings under a
// name in NVS, so it works for any remote that fits the carrier frequency
// (CONFIG_HOMEHUB_IR_CARRIER_HZ), air conditioners included.

// A name is 1-15 characters of lowercase letters, digits and '_'.
bool ir_remote_name_ok(const char *name);

// Waits up to timeout_ms for a button press and stores it as `name`.
// Returns ESP_ERR_TIMEOUT when nothing usable arrived; *pulses is the length.
esp_err_t ir_remote_learn(const char *name, int timeout_ms, int *pulses);

// Plays back a learned button. ESP_ERR_NOT_FOUND when `name` wasn't learned.
esp_err_t ir_remote_send(const char *name);

// Writes the learned names, comma separated, into out. Returns how many.
int ir_remote_list(char *out, size_t cap);
