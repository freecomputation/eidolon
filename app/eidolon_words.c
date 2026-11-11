#include "eidolon.h"

/* Word bank for the voice / spirit-box word cue. Words are picked with the
 * hardware RNG, weighted by callers only through *when* they trigger an
 * event (measured surges), never by steering which word appears. */
static const char* const eidolon_word_bank[] = {
    "cold",    "dark",    "listen",  "behind", "below",   "above",  "door",
    "window",  "water",   "light",   "name",   "help",    "stay",   "leave",
    "wait",    "here",    "there",   "close",  "far",     "yes",    "no",
    "mother",  "father",  "child",   "buried", "lost",    "found",  "remember",
    "forget",  "breathe", "static",  "signal", "echo",    "shadow", "knock",
    "twice",   "once",    "again",   "soon",   "never",   "always", "watching",
    "hiding",  "coming",  "going",   "sleep",  "wake",    "dream",  "stone",
    "earth",   "fire",    "air",     "river",  "bridge",  "house",  "room",
    "stairs",  "mirror",  "picture", "book",   "key",     "bell",   "song",
    "prayer",  "amen",    "mercy",   "truth",  "secret",  "guilty", "innocent",
};

#define EIDOLON_WORD_BANK_COUNT (sizeof(eidolon_word_bank) / sizeof(eidolon_word_bank[0]))

const char* eidolon_words_pick(void) {
    return eidolon_word_bank[furi_hal_random_get() % EIDOLON_WORD_BANK_COUNT];
}

void eidolon_words_init(EidolonApp* app) {
    memset(&app->words, 0, sizeof(app->words));
}

void eidolon_words_trigger(EidolonApp* app, const char* source) {
    EidolonWords* w = &app->words;
    if(w->spelling) {
        if(w->pending < 3) w->pending++;
        return;
    }
    const char* word = eidolon_words_pick();
    strncpy(w->current, word, sizeof(w->current) - 1);
    w->current[sizeof(w->current) - 1] = '\0';
    strncpy(w->source, source, sizeof(w->source) - 1);
    w->source[sizeof(w->source) - 1] = '\0';
    w->shown = 0;
    w->spelling = true;
    w->next_letter_tick = furi_get_tick();
    w->done_tick = 0;
}

void eidolon_words_tick(EidolonApp* app) {
    EidolonWords* w = &app->words;
    if(!w->spelling) {
        if(w->pending > 0) {
            w->pending--;
            eidolon_words_trigger(app, w->source[0] ? w->source : "VOICE");
        }
        return;
    }
    uint32_t now = furi_get_tick();
    if(now < w->next_letter_tick) return;
    size_t len = strlen(w->current);
    if(w->shown < len) {
        w->shown++;
        /* garbled syllable blip per letter, pitch wanders with the RNG */
        eidolon_beep(app, 300.0f + (float)(furi_hal_random_get() % 600), 0.25f, 45);
        w->next_letter_tick = now + 170;
        return;
    }
    /* word complete */
    w->spelling = false;
    w->done_tick = now;
    if(w->history_count < EIDOLON_WORD_HISTORY) {
        memmove(
            &w->history[1], &w->history[0], (EIDOLON_WORD_HISTORY - 1) * EIDOLON_WORD_MAX);
    } else {
        memmove(
            &w->history[1], &w->history[0], (EIDOLON_WORD_HISTORY - 1) * EIDOLON_WORD_MAX);
    }
    strncpy(w->history[0], w->current, EIDOLON_WORD_MAX - 1);
    w->history[0][EIDOLON_WORD_MAX - 1] = '\0';
    if(w->history_count < EIDOLON_WORD_HISTORY) w->history_count++;
    eidolon_log_event(app, w->source[0] ? w->source : "VOICE", "word: %s", w->current);
    eidolon_cue_word(app);
    if(w->pending > 0) {
        w->pending--;
        eidolon_words_trigger(app, w->source);
    }
}
