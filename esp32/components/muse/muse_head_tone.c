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

