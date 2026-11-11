#include "eidolon.h"
#include "eidolon_hw.h"

/* Spirit box: a real Sub-GHz sweep. The CC1101's raw demodulated RX
 * bitstream is mirrored onto the speaker pin in hardware, so what comes
 * out of the speaker is genuine RF static at each step, not a tone.
 * The 64-step ribbon shows the noise floor per step; a carrier that
 * stands far enough above the floor across repeated passes counts as a
 * lock-on and cues a word. */

static const struct {
    uint32_t start;
    uint32_t stop;
    const char* name;
} box_bands[3] = {
    {300000000, 348000000, "300-348"},
    {387000000, 464000000, "387-464"},
    {779000000, 928000000, "779-928"},
};

static uint32_t box_freq(const BoxState* b, uint8_t step) {
    return b->f_start + (uint32_t)(((uint64_t)(b->f_stop - b->f_start) * step) / (BOX_STEPS - 1));
}

static void box_apply_band(EidolonApp* app) {
    BoxState* b = &app->box;
    b->f_start = box_bands[b->band].start;
    b->f_stop = box_bands[b->band].stop;
    b->step = 0;
    b->floor_valid = false;
    b->floor_dbm = -120.0f;
    b->last_above_step = -10;
    b->lock_hits = 0;
    for(uint8_t i = 0; i < BOX_STEPS; i++) b->ribbon[i] = -130.0f;
    eidolon_radio_tune(app, box_freq(b, 0));
    b->step_deadline = furi_get_tick() + b->dwell_ms;
    b->sampled = false;
}

static void box_enter(EidolonApp* app) {
    BoxState* b = &app->box;
    memset(b, 0, sizeof(*b));
    b->band = 0;
    b->dwell_ms = 200;
    b->muted = false;
    eidolon_radio_start(app);
    box_apply_band(app);
    if(!b->muted) eidolon_radio_mirror(app, true);
    furi_hal_power_insomnia_enter();
    eidolon_log_event(app, "BOX", "spirit box open, band %s MHz", box_bands[b->band].name);
}

static void box_exit(EidolonApp* app) {
    eidolon_radio_mirror(app, false);
    eidolon_radio_stop(app);
    furi_hal_power_insomnia_exit();
    eidolon_led_off(app);
    eidolon_log_event(app, "BOX", "spirit box close");
}

static void box_update_floor(BoxState* b) {
    /* floor = mean of the lowest 16 steps of the finished sweep */
    float lowest[16];
    for(uint8_t i = 0; i < 16; i++) lowest[i] = 1e9f;
    for(uint8_t i = 0; i < BOX_STEPS; i++) {
        float v = b->ribbon[i];
        for(uint8_t j = 0; j < 16; j++) {
            if(v < lowest[j]) {
                memmove(&lowest[j + 1], &lowest[j], (15 - j) * sizeof(float));
                lowest[j] = v;
                break;
            }
        }
    }
    float sum = 0;
    for(uint8_t i = 0; i < 16; i++) sum += lowest[i];
    b->floor_dbm = sum / 16.0f;
    b->floor_valid = true;
}

static void box_tick(EidolonApp* app) {
    BoxState* b = &app->box;
    uint32_t now = furi_get_tick();

    /* sample RSSI once per step, late in the dwell so the receiver settled */
    if(!b->sampled && now + 40 >= b->step_deadline) {
        b->sampled = true;
        b->rssi_now = eidolon_radio_rssi(app);
        b->ribbon[b->step] = b->rssi_now;
        app->activity_rf = (b->rssi_now + 120.0f) / 80.0f;
        if(app->activity_rf < 0) app->activity_rf = 0;
        if(app->activity_rf > 1) app->activity_rf = 1;

        /* lock-on detection against the sweep floor */
        if(b->floor_valid && b->rssi_now > b->floor_dbm + 14.0f) {
            int16_t s = (int16_t)b->step;
            if(s - b->last_above_step <= 2 || s == b->last_above_step) {
                b->lock_hits++;
            } else {
                b->lock_hits = 1;
            }
            b->last_above_step = s;
            if(b->lock_hits >= 3 && now >= b->lock_cooldown_until) {
                b->lock_hits = 0;
                b->lock_cooldown_until = now + 20000;
                char fb[16];
                eidolon_fmt_1dp(fb, sizeof(fb), (float)box_freq(b, b->step) / 1000000.0f);
                char rb[16];
                eidolon_fmt_1dp(rb, sizeof(rb), b->rssi_now);
                eidolon_log_event(app, "BOX", "lock at %s MHz, RSSI %s dBm", fb, rb);
                eidolon_surge(app, 0.7f);
                eidolon_words_trigger(app, "BOX");
            }
        }
    }

    if(now >= b->step_deadline) {
        b->step++;
        if(b->step >= BOX_STEPS) {
            b->step = 0;
            box_update_floor(b);
        }
        eidolon_radio_tune(app, box_freq(b, b->step));
        b->step_deadline = now + b->dwell_ms;
        b->sampled = false;
    }
}

static int box_col_h(float dbm) {
    float t = (dbm + 120.0f) / 80.0f; /* -120..-40 -> 0..1 */
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    return (int)(t * 25.0f);
}

static void box_draw(EidolonApp* app, Canvas* canvas) {
    BoxState* b = &app->box;
    char buf[48];
    char num[16];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "SPIRIT BOX");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%sM %lums", box_bands[b->band].name, (unsigned long)b->dwell_ms);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, buf);

    /* ribbon */
    for(uint8_t i = 0; i < BOX_STEPS; i++) {
        int h = box_col_h(b->ribbon[i]);
        int x = i * 2;
        if(h > 0) canvas_draw_line(canvas, x, 42, x, 42 - h);
        if(i == b->step) canvas_draw_dot(canvas, x, 12);
    }
    /* lock threshold line */
    if(b->floor_valid) {
        int ty = 42 - box_col_h(b->floor_dbm + 14.0f);
        for(int x = 0; x < 128; x += 4) canvas_draw_dot(canvas, x, ty);
    }

    /* current step readout */
    eidolon_fmt_1dp(num, sizeof(num), (float)box_freq(b, b->step) / 1000000.0f);
    char rb[16];
    eidolon_fmt_1dp(rb, sizeof(rb), b->rssi_now);
    snprintf(buf, sizeof(buf), "%s MHz  %s dBm", num, rb);
    canvas_draw_str(canvas, 2, 52, buf);
    canvas_draw_str_aligned(
        canvas, 127, 52, AlignRight, AlignBottom, b->muted ? "MUTED" : "SWEEP");

    /* word cue */
    EidolonWords* w = &app->words;
    if(w->spelling || (w->done_tick && furi_get_tick() - w->done_tick < 5000)) {
        canvas_set_font(canvas, FontPrimary);
        char partial[EIDOLON_WORD_MAX + 1];
        size_t n = w->shown;
        if(n > strlen(w->current)) n = strlen(w->current);
        memcpy(partial, w->current, n);
        partial[n] = '\0';
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, partial);
    }
}

static bool box_input(EidolonApp* app, InputEvent* event) {
    BoxState* b = &app->box;
    if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            if(b->dwell_ms < 500) b->dwell_ms += 50;
            return true;
        }
        if(event->key == InputKeyDown) {
            if(b->dwell_ms > 100) b->dwell_ms -= 50;
            return true;
        }
        if(event->key == InputKeyLeft) {
            b->band = (b->band + 2) % 3;
            box_apply_band(app);
            return true;
        }
        if(event->key == InputKeyRight) {
            b->band = (b->band + 1) % 3;
            box_apply_band(app);
            return true;
        }
    }
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        b->muted = !b->muted;
        eidolon_radio_mirror(app, !b->muted);
        eidolon_log_event(app, "BOX", b->muted ? "muted" : "unmuted");
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_box = {
    .enter = box_enter,
    .exit = box_exit,
    .tick = box_tick,
    .draw = box_draw,
    .input = box_input,
};
