#include "eidolon.h"
#include "eidolon_hw.h"

/* Fusion watch: every sensor the kit has, sampled together on one
 * timeline - Sub-GHz field, REM jitter, infrared events, reader
 * fields, battery-temperature trend - combined into one presence
 * level and written to the same session log. */

static const uint32_t fusion_freqs[8] = {
    300000000,
    307000000,
    314000000,
    321000000,
    328000000,
    335000000,
    342000000,
    348000000,
};

static const char* fusion_state_name(float p) {
    if(p < 0.08f) return "QUIET";
    if(p < 0.25f) return "STIRRING";
    if(p < 0.5f) return "ACTIVE";
    if(p < 0.75f) return "STRONG";
    return "CONTACT";
}

static void fusion_enter(EidolonApp* app) {
    FusionState* f = &app->fusion;
    memset(f, 0, sizeof(*f));
    f->rf_floor = -130.0f;
    f->rf_rssi = -130.0f;
    eidolon_radio_start(app);
    eidolon_fd_start(app);
    eidolon_adc_start(app);
    eidolon_adc_config(app, FuriHalAdcOversampleNone, FuriHalAdcSamplingtime2_5);
    eidolon_ir_start(app);
    furi_hal_power_insomnia_enter();
    f->temp_start = eidolon_battery_temp();
    f->temp_now = f->temp_start;
    f->last_temp_tick = furi_get_tick();
    eidolon_radio_tune(app, fusion_freqs[0]);
    eidolon_log_event(app, "FUSION", "fusion watch open");
}

static void fusion_exit(EidolonApp* app) {
    FusionState* f = &app->fusion;
    eidolon_ir_stop(app);
    eidolon_adc_stop(app);
    eidolon_fd_stop(app);
    eidolon_radio_stop(app);
    furi_hal_power_insomnia_exit();
    eidolon_led_off(app);
    eidolon_log_event(app, "FUSION", "fusion watch close, peak presence %d%%", (int)(f->peak_presence * 100.0f));
}

static void fusion_tick(EidolonApp* app) {
    FusionState* f = &app->fusion;
    uint32_t now = furi_get_tick();
    eidolon_fd_tick(app);

    /* RF: one sweep step per tick */
    f->rf_rssi = eidolon_radio_rssi(app);
    f->radio_step = (f->radio_step + 1) % 8;
    eidolon_radio_tune(app, fusion_freqs[f->radio_step]);
    if(f->rf_rssi < f->rf_floor || f->rf_floor < -129.0f) {
        f->rf_floor = f->rf_rssi;
    } else {
        f->rf_floor += (f->rf_rssi - f->rf_floor) * 0.002f;
    }
    float rf = (f->rf_rssi - f->rf_floor) * 4.0f;
    if(rf < 0) rf = 0;
    if(rf > 100) rf = 100;
    rf /= 100.0f;
    app->activity_rf = rf;

    /* REM jitter */
    f->rem_jitter = eidolon_adc_jitter(app, eidolon_adc.ch_pc1, 24);
    if(!f->rem_zeroed) {
        f->rem_base = f->rem_jitter;
        f->rem_zeroed = true;
    } else if(f->rem_jitter <= f->rem_base + 20) {
        f->rem_base = (uint16_t)(((uint32_t)f->rem_base * 15 + f->rem_jitter) / 16);
    }
    float rem = 0;
    if(f->rem_jitter > f->rem_base) {
        rem = (float)(f->rem_jitter - f->rem_base) / 46.0f;
        if(rem > 1) rem = 1;
    }
    app->activity_rem = rem;

    /* IR: drain and log on the shared timeline */
    EidolonIrEvent ev;
    while(eidolon_ir_poll(app, &ev)) {
        eidolon_ir_log_event(app, "FUSION", &ev);
    }
    float ir = 0;
    if(app->ir_events_total > 0) {
        uint32_t age = now - app->ir_last_tick;
        if(age < 8000) ir = 1.0f - (float)age / 8000.0f;
    }

    /* temperature trend */
    if(now - f->last_temp_tick >= 5000) {
        f->last_temp_tick = now;
        f->temp_now = eidolon_battery_temp();
    }
    float cold = (f->temp_start - f->temp_now) / 1.5f;
    if(cold < 0) cold = 0;
    if(cold > 1) cold = 1;

    float field = app->field_present ? 1.0f : 0.0f;

    float target = 0.35f * rf + 0.25f * rem + 0.20f * ir + 0.10f * field + 0.10f * cold;
    f->presence += (target - f->presence) * 0.2f;
    if(f->presence > f->peak_presence) f->peak_presence = f->presence;

    /* contact edge */
    if(!f->contact && f->presence >= 0.65f) {
        f->contact = true;
        eidolon_log_event(app, "FUSION", "CONTACT: presence %d%%", (int)(f->presence * 100.0f));
        eidolon_surge(app, 0.85f);
    } else if(f->contact && f->presence < 0.4f) {
        f->contact = false;
    }

    /* periodic summary on the one timeline */
    if(now - f->last_summary_tick >= 15000) {
        f->last_summary_tick = now;
        eidolon_log_event(
            app,
            "FUSION",
            "presence %d%% rf %d%% rem %d%% ir %d%% field %d cold %d%%",
            (int)(f->presence * 100.0f),
            (int)(rf * 100.0f),
            (int)(rem * 100.0f),
            (int)(ir * 100.0f),
            app->field_present ? 1 : 0,
            (int)(cold * 100.0f));
    }
}

static void fusion_bar(Canvas* canvas, int y, const char* label, float v) {
    char buf[16];
    canvas_draw_str(canvas, 2, y + 5, label);
    canvas_draw_frame(canvas, 30, y, 70, 6);
    int fill = (int)(v * 68.0f);
    if(fill > 0) canvas_draw_box(canvas, 31, y + 1, fill, 4);
    snprintf(buf, sizeof(buf), "%d%%", (int)(v * 100.0f));
    canvas_draw_str(canvas, 104, y + 5, buf);
}

static void fusion_draw(EidolonApp* app, Canvas* canvas) {
    FusionState* f = &app->fusion;
    char buf[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "FUSION");
    snprintf(buf, sizeof(buf), "%d%%", (int)(f->presence * 100.0f));
    canvas_draw_str_aligned(canvas, 90, 10, AlignRight, AlignBottom, buf);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, fusion_state_name(f->presence));
    canvas_set_font(canvas, FontSecondary);

    float rf = app->activity_rf;
    float rem = app->activity_rem;
    float ir = 0;
    if(app->ir_events_total > 0) {
        uint32_t age = furi_get_tick() - app->ir_last_tick;
        if(age < 8000) ir = 1.0f - (float)age / 8000.0f;
    }
    float cold = (f->temp_start - f->temp_now) / 1.5f;
    if(cold < 0) cold = 0;
    if(cold > 1) cold = 1;

    fusion_bar(canvas, 14, "RF", rf);
    fusion_bar(canvas, 23, "REM", rem);
    fusion_bar(canvas, 32, "IR", ir);
    fusion_bar(canvas, 41, "COLD", cold);

    char rb[16];
    eidolon_fmt_1dp(rb, sizeof(rb), f->rf_rssi);
    snprintf(buf, sizeof(buf), "RF %s dBm  JIT %u", rb, f->rem_jitter);
    canvas_draw_str(canvas, 2, 57, buf);
    snprintf(
        buf,
        sizeof(buf),
        "IR %lu  FLD %s",
        (unsigned long)app->ir_events_total,
        app->field_present ? "YES" : "no");
    canvas_draw_str(canvas, 2, 64, buf);
}

static bool fusion_input(EidolonApp* app, InputEvent* event) {
    FusionState* f = &app->fusion;
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        f->rem_base = f->rem_jitter;
        f->rem_zeroed = true;
        f->rf_floor = f->rf_rssi;
        f->temp_start = f->temp_now;
        eidolon_log_event(app, "FUSION", "baselines re-zeroed");
        eidolon_cue_confirm(app);
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_fusion = {
    .enter = fusion_enter,
    .exit = fusion_exit,
    .tick = fusion_tick,
    .draw = fusion_draw,
    .input = fusion_input,
};
