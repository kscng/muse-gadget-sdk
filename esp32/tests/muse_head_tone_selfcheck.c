// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
// Run: cc -Wall -I components/muse tests/muse_head_tone_selfcheck.c components/muse/muse_head_tone.c -o /tmp/tone_check && /tmp/tone_check
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    // Muse's emotion tags at the start of a reply
    size_t used;
    int e = muse_emotion_parse("[excited] Great news!", &used);
    assert(e > 0 && used == 10 && !strcmp(muse_emotion_name(e), "excited") && muse_emotion_gesture(e) == G_BOUNCE);
    e = muse_emotion_parse("[Sad]   I couldn't find it.", &used);
    assert(e > 0 && used == 8 && muse_emotion_gesture(e) == G_DROOP);          // case-insensitive, spaces dropped
    e = muse_emotion_parse("[clears throat] Ahem.", &used);
    assert(e > 0 && !strcmp(muse_emotion_name(e), "clears throat") && muse_emotion_gesture(e) == G_NONE);
    assert(muse_emotion_parse("[1] First point", &used) == 0 && used == 0);     // not an emotion: left alone
    assert(muse_emotion_parse("[note] hello", &used) == 0);
    assert(muse_emotion_parse("Hello [excited]", &used) == 0);                  // only at the very start
    assert(muse_emotion_parse("[exci", &used) == -1);                           // still arriving
    assert(muse_emotion_parse("[a very long bracket with no end", &used) == 0); // never going to be a tag
    assert(muse_emotion_parse("", &used) == 0);
    puts("muse_head_tone ok");
    return 0;
}
