// SPDX-License-Identifier: Apache-2.0 (StackChan additions to the Muse Gadget SDK)
/*
 * Reads a reply's tone from its first sentence: pure text, so it runs on a computer too
 * (tests/muse_head_tone_selfcheck.c).
 */
#include "muse_head_tone.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static bool has_word(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (const char *p = s; (p = strstr(p, w)) != NULL; p++) {
        bool lo = p == s || !isalnum((unsigned char)p[-1]);
        char c = p[n];
        bool hi = !c || !(isalnum((unsigned char)c) || c == '\'');
        if (lo && hi) return true;
    }
    return false;
}

static bool has_any(const char *s, const char *const *list, bool ascii_words)
{
    for (; *list; list++) {
        if (ascii_words ? has_word(s, *list) : strstr(s, *list) != NULL) return true;
    }
    return false;
}

/* The first sentence decides: a question tilts, a refusal shakes, agreement nods, cheer bounces. */
uint8_t muse_head_tone(const char *text)
{
    static const char *const yes_override[] = { "no problem", "no worries", "not a problem", NULL };
    static const char *const yes_override_cjk[] = { "冇問題", "沒問題", NULL };
    static const char *const no_en[] = { "no", "not", "cannot", "can't", "can’t", "unable", "sorry", "unfortunately",
                                         "won't", "won’t", "don't", "don’t", "never", NULL };
    static const char *const no_cjk[] = { "唔得", "唔可以", "做唔到", "對唔住", "抱歉", "不能", "不可以", "冇辦法",
                                          "沒辦法", "沒有辦法", "無法", NULL };
    static const char *const yes_en[] = { "yes", "yeah", "yep", "sure", "of course", "absolutely", "okay", "ok",
                                          "done", "great", "certainly", "got it", NULL };
    static const char *const yes_cjk[] = { "好呀", "好啊", "係呀", "係啊", "可以", "當然", "搞掂", "完成", "好的",
                                           "明白", NULL };
    char s[160];
    size_t n = 0;
    bool question = false, cheer = false;
    for (size_t i = 0; text[i] && n < sizeof(s) - 1; i++) {
        unsigned char c = (unsigned char)text[i];
        if ((c == '?' || c == '.' || c == '!' || c == '\n') && n > 3) {
            question = c == '?';
            cheer = c == '!';
            break;
        }
        if (!strncmp(text + i, "？", 3) && n > 3) { question = true; break; }
        if (!strncmp(text + i, "！", 3) && n > 3) { cheer = true; break; }
        if (!strncmp(text + i, "。", 3) && n > 3) break;
        s[n++] = c < 128 ? (char)tolower(c) : (char)c;
    }
    s[n] = 0;
    if (question) return G_TILT;
    if (has_any(s, yes_override, true) || has_any(s, yes_override_cjk, false)) return G_NOD;
    if (has_any(s, no_en, true) || has_any(s, no_cjk, false)) return G_SHAKE;
    if (has_any(s, yes_en, true) || has_any(s, yes_cjk, false)) return G_NOD;
    return cheer ? G_BOUNCE : G_NONE;
}

/* Muse's own emotion tags (ElevenLabs audio tags). Order matters: it is the index used elsewhere. */
static const struct {
    const char *name;
    uint8_t gesture;
} EMOTIONS[MUSE_EMOTIONS] = {
    { "", G_NONE },
    { "excited", G_BOUNCE },     { "playful", G_BOUNCE },   { "peaceful", G_NONE },   { "amazed", G_BOUNCE },
    { "anxious", G_SHAKE },      { "frustrated", G_SHAKE }, { "scared", G_SHAKE },    { "tired", G_DROOP },
    { "curious", G_TILT },       { "angry", G_SHAKE },      { "sad", G_DROOP },       { "laughs", G_BOUNCE },
    { "crying", G_DROOP },       { "sighs", G_DROOP },      { "mischievously", G_TILT },
};
/* "clears throat" is also accepted: it speaks as a tag but doesn't move the head. */
#define CLEARS_THROAT_IDX MUSE_EMOTIONS   /* one past the table */

const char *muse_emotion_name(int idx)
{
    if (idx == CLEARS_THROAT_IDX) return "clears throat";
    return idx > 0 && idx < MUSE_EMOTIONS ? EMOTIONS[idx].name : "";
}

uint8_t muse_emotion_gesture(int idx)
{
    return idx > 0 && idx < MUSE_EMOTIONS ? EMOTIONS[idx].gesture : G_NONE;
}

int muse_emotion_parse(const char *s, size_t *used)
{
    *used = 0;
    if (s[0] != '[') return 0;
    const char *close = strchr(s, ']');
    size_t n = close ? (size_t)(close - s) - 1 : strlen(s) - 1;   /* characters between the brackets so far */
    if (!close) return n < 16 ? -1 : 0;                            /* maybe still arriving */
    if (n == 0 || n > 15) return 0;
    char tag[16];
    for (size_t i = 0; i < n; i++) tag[i] = (char)tolower((unsigned char)s[1 + i]);
    tag[n] = 0;
    int found = 0;
    for (int i = 1; i <= MUSE_EMOTIONS && !found; i++) {
        if (!strcmp(tag, muse_emotion_name(i))) found = i;
    }
    if (!found) return 0;
    const char *rest = close + 1;
    while (*rest == ' ') rest++;
    *used = (size_t)(rest - s);
    return found;
}
