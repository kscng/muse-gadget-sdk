// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
// Run: cc -Wall -I components/muse tests/muse_head_tone_selfcheck.c components/muse/muse_head_tone.c -o /tmp/tone_check && /tmp/tone_check
#include <assert.h>
#include <stdio.h>

#include "muse_head_tone.h"

static void expect(const char *text, int want) {
    int got = muse_head_tone(text);
    if (got != want) printf("FAIL: \"%s\" -> %d, wanted %d\n", text, got, want);
    assert(got == want);
}

int main(void) {
    expect("Yes, I can do that for you.", G_NOD);
    expect("Sure! I'll set that up.", G_NOD);
    expect("No problem, it's done.", G_NOD);            // "no" inside a yes
    expect("No, I can't do that.", G_SHAKE);
    expect("Sorry, I couldn't find it.", G_SHAKE);
    expect("I know the answer.", G_NONE);               // "no" inside "know"
    expect("Would you like me to continue?", G_TILT);
    expect("Do you want tea? No problem either way.", G_TILT);   // first sentence decides
    expect("That's wonderful news!", G_BOUNCE);
    expect("The weather today is cloudy with some rain.", G_NONE);
    expect("好呀，我而家幫你搞掂。", G_NOD);
    expect("對唔住，我做唔到呢個。", G_SHAKE);
    expect("冇問題，一陣就好。", G_NOD);
    expect("你想食咩？", G_TILT);
    expect("", G_NONE);
    puts("muse_head_tone ok");
    return 0;
}
