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

#include <stddef.h>
#include <stdint.h>

// One half of a captured RMT symbol: how long the receiver output held `level`.
// A duration of 0 ends the capture. An IR receiver module idles high and goes
// low while it sees the carrier, so level 0 is a mark.
typedef struct {
    uint16_t ticks;
    uint8_t level;
} ir_half_t;

// Turns captured halves into alternating mark/space durations in microseconds,
// starting with a mark and ending with one (the trailing idle is dropped).
// Returns how many were written, or 0 when the capture is too short to be a
// button press (ambient noise) or doesn't fit in `cap`.
size_t ir_pulses_from_halves(const ir_half_t *halves, size_t n, uint32_t us_per_tick,
                             uint16_t *out, size_t cap);
