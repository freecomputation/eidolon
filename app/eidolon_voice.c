#include "eidolon.h"

/* Voice: word events driven by measured surges in the kit's real
 * instruments (field, REM, IR activity) plus the hardware RNG. This is
 * the theater layer and is labeled as such: the meters are real, the
 * words are atmosphere. Words arrive on their own when activity is
 * high, or in answer to a question (OK). */

static void voice_enter(EidolonApp* app) {
    VoiceState* v = &app->voice;
    memset(v, 0, sizeof(*v));
    v->next_auto_tick = furi_get_tick() + 8000;
    eidolon_log_event(app, "VOICE", "voice open");
}

static void voice_exit(EidolonApp* app) {
    eidolon_log_event(app, "VOICE", "voice close");
}

static void voice_tick(EidolonApp* app) {
    VoiceState* v = &app->voice;
    uint32_t now = furi_get_tick();
    float act = eidolon_activity(app);

    if(v->asking && now >= v->answer_at) {
        v->asking = false;
        eidolon_words_trigger(app, "VOICE");
        /* answers often run to two or three words */
        uint32_t extra = furi_hal_random_get() % 3;
        for(uint32_t i = 0; i < extra; i++) eidolon_words_trigger(app, "VOICE");
    }

    if(now >= v->next_auto_tick) {
        if(act > 0.22f) {
            eidolon_words_trigger(app, "VOICE");
        }
        uint32_t base = 16000 + (furi_hal_random_get() % 30000);
        uint32_t scaled = (uint32_t)((float)base * (1.25f - act));
        if(scaled < 5000) scaled = 5000;
        v->next_auto_tick = now + scaled;
    }
}

static void voice_draw(EidolonApp* app, Canvas* canvas) {
    EidolonWords* w = &app->words;
    VoiceState* v = &app->voice;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "VOICE");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 127, 9, AlignRight, AlignBottom, v->asking ? "LISTENING" : "IDLE");

    /* current word, spelled out letter by letter */
    if(w->spelling || w->current[0]) {
        char partial[EIDOLON_WORD_MAX + 1];
        size_t n = w->shown;
        if(n > strlen(w->current)) n = strlen(w->current);
        memcpy(partial, w->current, n);
        partial[n] = '\0';
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignBottom, partial);
        canvas_set_font(canvas, FontSecondary);
    }

    /* last three words */
    int y = 46;
    for(uint8_t i = 0; i < w->history_count && i < EIDOLON_WORD_HISTORY; i++) {
        canvas_draw_str(canvas, 10, y, w->history[i]);
        y += 8;
    }

    canvas_draw_str(canvas, 2, 63, "OK: ASK");
    canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, "THEATER - see ABOUT");
}

static bool voice_input(EidolonApp* app, InputEvent* event) {
    VoiceState* v = &app->voice;
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        if(!v->asking) {
            v->asking = true;
            v->answer_at = furi_get_tick() + 1200 + (furi_hal_random_get() % 2500);
            eidolon_log_event(app, "VOICE", "question asked");
        }
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_voice = {
    .enter = voice_enter,
    .exit = voice_exit,
    .tick = voice_tick,
    .draw = voice_draw,
    .input = voice_input,
};
