#include "eidolon.h"
#include "eidolon_hw.h"

/* REM pod: a bare copper wire on pin 15 (PC1) leaves a floating
 * high-impedance ADC node coupled to ambient mains field; a nearby body
 * changes that coupling. The signal is the peak-to-peak spread of 24
 * back-to-back raw samples - the jitter, not an average. Alerts are
 * judged against a slowly tracking baseline, so a wire that is simply
 * present (or absent) reads quiet. */

#define REM_SAMPLES 24
#define REM_MIN_JITTER 14

static uint16_t rem_threshold(const RemState* r) {
    return r->base + (uint16_t)((11 - r->sensitivity) * 8 + 6);
}

static void rem_alert_stop(EidolonApp* app) {
    RemState* r = &app->rem;
    if(!r->alert) return;
    r->alert = false;
    eidolon_tone_stop(app);
    eidolon_alert_feedback_stop(app);
    eidolon_log_event(app, "REM", "alert end, jitter %u", r->jitter);
}

static void rem_enter(EidolonApp* app) {
    RemState* r = &app->rem;
    memset(r, 0, sizeof(*r));
    r->sensitivity = 6;
    r->volume = 7;
    eidolon_adc_start(app);
    eidolon_adc_config(app, FuriHalAdcOversampleNone, FuriHalAdcSamplingtime2_5);
    furi_hal_power_insomnia_enter();
    eidolon_log_event(app, "REM", "rem pod open (wire on pin 15)");
}

static void rem_exit(EidolonApp* app) {
    rem_alert_stop(app);
    eidolon_adc_stop(app);
    furi_hal_power_insomnia_exit();
    eidolon_led_off(app);
    eidolon_log_event(app, "REM", "rem pod close");
}

static void rem_tick(EidolonApp* app) {
    RemState* r = &app->rem;
    r->jitter = eidolon_adc_jitter(app, eidolon_adc.ch_pc1, REM_SAMPLES);

    if(!r->zeroed) {
        r->base = r->jitter;
        r->zeroed = true;
    }

    uint16_t thresh = rem_threshold(r);
    bool over = r->jitter > thresh && r->jitter > REM_MIN_JITTER;

    /* slow baseline tracking while quiet */
    if(!over && !r->alert) {
        r->base = (uint16_t)(((uint32_t)r->base * 15 + r->jitter) / 16);
    }

    if(over) {
        if(r->over_ticks < 255) r->over_ticks++;
    } else {
        r->over_ticks = 0;
    }

    if(!r->alert && r->over_ticks >= 2) {
        r->alert = true;
        uint16_t overage = r->jitter - thresh;
        float freq = 600.0f + (float)(overage > 200 ? 200 : overage) * 2.0f;
        eidolon_tone_start(app, freq, (float)r->volume / 10.0f * 0.9f);
        eidolon_log_event(
            app, "REM", "alert: jitter %u over base %u (sens %u)", r->jitter, r->base, r->sensitivity);
        eidolon_alert_feedback_start(app);
    } else if(r->alert && !over) {
        rem_alert_stop(app);
    }

    if(r->alert) {
        app->activity_rem = 1.0f;
    } else {
        uint16_t thresh_now = rem_threshold(r);
        float frac = 0;
        if(r->jitter > r->base) {
            float span = (float)(thresh_now - r->base);
            if(span > 0) frac = (float)(r->jitter - r->base) / span;
        }
        if(frac > 1) frac = 1;
        app->activity_rem = frac * 0.8f;
    }
}

static void rem_draw(EidolonApp* app, Canvas* canvas) {
    RemState* r = &app->rem;
    char buf[48];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "REM POD");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, "WIRE PIN 15");

    if(r->alert) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "CONTACT");
        canvas_set_font(canvas, FontSecondary);
    }

    snprintf(buf, sizeof(buf), "JITTER %u", r->jitter);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 44, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "BASE %u  THR %u", r->base, rem_threshold(r));
    canvas_draw_str(canvas, 66, 44, buf);

    /* jitter bar against threshold */
    canvas_draw_frame(canvas, 2, 48, 124, 6);
    uint16_t scale = rem_threshold(r) * 2;
    if(scale == 0) scale = 1;
    int fill = (int)((uint32_t)r->jitter * 122 / scale);
    if(fill > 122) fill = 122;
    if(fill > 0) canvas_draw_box(canvas, 3, 49, fill, 4);
    int thr_x = 2 + (int)((uint32_t)rem_threshold(r) * 124 / scale);
    canvas_draw_line(canvas, thr_x, 47, thr_x, 55);

    snprintf(buf, sizeof(buf), "SENS %u (L/R)   VOL %u (U/D)   OK: ZERO", r->sensitivity, r->volume);
    canvas_draw_str(canvas, 2, 63, buf);
}

static bool rem_input(EidolonApp* app, InputEvent* event) {
    RemState* r = &app->rem;
    if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        if(event->key == InputKeyLeft) {
            if(r->sensitivity > 1) r->sensitivity--;
            return true;
        }
        if(event->key == InputKeyRight) {
            if(r->sensitivity < 10) r->sensitivity++;
            return true;
        }
        if(event->key == InputKeyUp) {
            if(r->volume < 10) r->volume++;
            if(r->alert) {
                eidolon_tone_stop(app);
                eidolon_tone_start(app, 700.0f, (float)r->volume / 10.0f * 0.9f);
            }
            return true;
        }
        if(event->key == InputKeyDown) {
            if(r->volume > 0) r->volume--;
            if(r->alert) {
                eidolon_tone_stop(app);
                eidolon_tone_start(app, 700.0f, (float)r->volume / 10.0f * 0.9f);
            }
            return true;
        }
    }
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        r->base = r->jitter;
        r->zeroed = true;
        eidolon_log_event(app, "REM", "re-zeroed, base %u", r->base);
        eidolon_cue_confirm(app);
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_rem = {
    .enter = rem_enter,
    .exit = rem_exit,
    .tick = rem_tick,
    .draw = rem_draw,
    .input = rem_input,
};
