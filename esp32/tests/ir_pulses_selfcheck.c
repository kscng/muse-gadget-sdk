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

// Run: cc -Wall -I main tests/ir_pulses_selfcheck.c main/ir_pulses.c -o /tmp/ir_check && /tmp/ir_check

#include <assert.h>
#include <stdio.h>

#include "ir_pulses.h"

int main(void) {
    // 2 us ticks: idle, then 3.5 ms mark, 1.75 ms space, 8 short pairs, then the idle timeout.
    ir_half_t h[64];
    size_t n = 0;
    h[n++] = (ir_half_t){ 500, 1 };
    h[n++] = (ir_half_t){ 1750, 0 };
    h[n++] = (ir_half_t){ 875, 1 };
    for (int i = 0; i < 8; i++) {
        h[n++] = (ir_half_t){ 225, 0 };
        h[n++] = (ir_half_t){ 225, 1 };
    }
    h[n++] = (ir_half_t){ 25000, 1 };   // idle timeout: same level as the last space, merged and dropped
    h[n++] = (ir_half_t){ 0, 0 };

    uint16_t out[64];
    size_t got = ir_pulses_from_halves(h, n, 2, out, 64);
    assert(got == 17);                         // 8 pairs + the leader pair, minus the dropped ending space
    assert(out[0] == 3500 && out[1] == 1750);  // leader, in microseconds
    assert(out[2] == 450 && out[3] == 450);
    assert(out[16] == 450);                    // ends on a mark

    assert(ir_pulses_from_halves(h, n, 2, out, 5) == 0);   // doesn't fit: refused, not truncated

    ir_half_t noise[3] = { { 40, 0 }, { 30, 1 }, { 0, 0 } };   // a blip of ambient IR
    assert(ir_pulses_from_halves(noise, 3, 2, out, 64) == 0);

    puts("ir_pulses ok");
    return 0;
}
