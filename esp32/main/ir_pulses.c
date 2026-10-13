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

#include "ir_pulses.h"

#define MIN_PULSES 8   // shorter than any remote button; ambient light and noise

static uint16_t clamp_us(uint32_t us) {
    return us > 65535 ? 65535 : (uint16_t)us;
}

size_t ir_pulses_from_halves(const ir_half_t *halves, size_t n, uint32_t us_per_tick,
                             uint16_t *out, size_t cap) {
    size_t i = 0, count = 0;
    while (i < n && halves[i].ticks && halves[i].level != 0) i++;   // idle before the first mark
    uint32_t run = 0;
    int level = 0;   // the run being accumulated; starts on a mark
    for (; i < n && halves[i].ticks; i++) {
        if (halves[i].level == level) {
            run += (uint32_t)halves[i].ticks * us_per_tick;
            continue;
        }
        if (count == cap) return 0;
        out[count++] = clamp_us(run);
        level = halves[i].level ? 1 : 0;
        run = (uint32_t)halves[i].ticks * us_per_tick;
    }
    if (level == 0 && run) {   // ended on a mark; an ending space is only the idle gap
        if (count == cap) return 0;
        out[count++] = clamp_us(run);
    }
    return count >= MIN_PULSES ? count : 0;
}
