#include "eidolon.h"
#include "eidolon_hw.h"

static const uint32_t field_sweep_freqs[FIELD_SWEEP_STEPS] = {
    300000000,
    307000000,
    314000000,
    321000000,
    328000000,
    335000000,
    342000000,
    348000000,
};

static const char* field_level_name(float level) {
    if(level < 5) return "QUIET";
    if(level < 25) return "FAINT";
    if(level < 50) return "ACTIVE";
    if(level < 75) return "STRONG";
    return "CONTACT";
}

/* ---------------- Hall sensor: Goertzel at 50/60 Hz ---------------- */

#define HALL_N 160

static float field_goertzel(const float* x, uint16_t n, float freq, float fs) {
    float w = 2.0f * (float)M_PI * freq / fs;
    float coeff = 2.0f * cosf(w);
    float s1 = 0, s2 = 0;
    for(uint16_t i = 0; i < n; i++) {
        float s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    float re = s1 - s2 * cosf(w);
    float im = s2 * sinf(w);
    return sqrtf(re * re + im * im) / (float)n;
}

/* One burst measurement of the mains-hum field amplitude on pin 16. */
static float field_hall_measure(EidolonApp* app) {
    static float samples[HALL_N];
    uint32_t t0 = furi_get_tick();
    for(uint16_t i = 0; i < HALL_N; i++) {
        samples[i] = (float)eidolon_adc_read(app, eidolon_adc.ch_pc0);
        furi_delay_us(1350);
    }
    uint32_t elapsed = furi_get_tick() - t0;
    if(elapsed == 0) elapsed = 1;
    float fs = (float)HALL_N * 1000.0f / (float)elapsed;
    float mean = 0;
    for(uint16_t i = 0; i < HALL_N; i++) mean += samples[i];
    mean /= HALL_N;
    for(uint16_t i = 0; i < HALL_N; i++) samples[i] -= mean;
    float m50 = field_goertzel(samples, HALL_N, 50.0f, fs);
    float m60 = field_goertzel(samples, HALL_N, 60.0f, fs);
    return m50 > m60 ? m50 : m60;
}

/* ---------------- lifecycle ---------------- */

static void field_enter(EidolonApp* app) {
    FieldState* f = &app->field;
    memset(f, 0, sizeof(*f));
    f->source = FieldSrcSweep;
    f->band_freq_hz = 433920000;
    f->band_rssi = -130.0f;
    for(uint8_t i = 0; i < FIELD_SWEEP_STEPS; i++) f->rssi[i] = -130.0f;
    eidolon_radio_start(app);
    eidolon_fd_start(app);
    eidolon_adc_start(app);
    furi_hal_power_insomnia_enter();
    f->temp_start = eidolon_battery_temp();
    f->temp_now = f->temp_start;
    f->temp_valid = true;
    f->last_temp_tick = furi_get_tick();
    eidolon_radio_tune(app, field_sweep_freqs[0]);
    eidolon_log_event(app, "FIELD", "field meter open");
}

static void field_exit(EidolonApp* app) {
    eidolon_fd_stop(app);
    eidolon_adc_stop(app);
    eidolon_radio_stop(app);
    furi_hal_power_insomnia_exit();
    eidolon_led_off(app);
    eidolon_log_event(app, "FIELD", "field meter close, peak %d", (int)app->field.peak);
}

static void field_zero(EidolonApp* app) {
    FieldState* f = &app->field;
    for(uint8_t i = 0; i < FIELD_SWEEP_STEPS; i++) f->baseline[i] = f->rssi[i];
    f->band_baseline = f->band_rssi;
    f->baseline_set = true;
    f->hall_base = f->hall_mag;
    f->hall_zeroed = true;
    f->peak = 0;
    eidolon_log_event(app, "FIELD", "baseline zeroed");
    eidolon_beep(app, 1200.0f, 0.3f, 60);
}

static void field_set_source(EidolonApp* app, FieldSource source) {
    FieldState* f = &app->field;
    f->source = source;
    if(source == FieldSrcBand) {
        eidolon_radio_tune(app, f->band_freq_hz);
    } else if(source == FieldSrcSweep) {
        f->step = 0;
        eidolon_radio_tune(app, field_sweep_freqs[0]);
    } else if(source == FieldSrcHall) {
        eidolon_adc_config(app, FuriHalAdcOversampleNone, FuriHalAdcSamplingtime47_5);
    }
    eidolon_beep(app, 900.0f, 0.25f, 40);
}

/* ---------------- tick ---------------- */

static void field_tick(EidolonApp* app) {
    FieldState* f = &app->field;
    uint32_t now = furi_get_tick();
    eidolon_fd_tick(app);

    float raw_level = 0;

    if(f->source == FieldSrcSweep) {
        if(now - f->last_sample_tick >= 55) {
            f->last_sample_tick = now;
            f->rssi[f->step] = eidolon_radio_rssi(app);
            if(!f->baseline_set) {
                f->baseline[f->step] = f->rssi[f->step];
                if(f->step == FIELD_SWEEP_STEPS - 1) f->baseline_set = true;
            }
            f->step = (f->step + 1) % FIELD_SWEEP_STEPS;
            eidolon_radio_tune(app, field_sweep_freqs[f->step]);
        }
        float best = 0;
        for(uint8_t i = 0; i < FIELD_SWEEP_STEPS; i++) {
            float d = (f->rssi[i] - f->baseline[i]) * 4.0f;
            if(d > best) best = d;
        }
        raw_level = best;
    } else if(f->source == FieldSrcBand) {
        if(now - f->last_sample_tick >= 50) {
            f->last_sample_tick = now;
            f->band_rssi = eidolon_radio_rssi(app);
            if(!f->baseline_set) {
                f->band_baseline = f->band_rssi;
                f->baseline_set = true;
            }
        }
        raw_level = (f->band_rssi - f->band_baseline) * 4.0f;
    } else { /* Hall */
        if(now - f->hall_last_tick >= 450) {
            f->hall_last_tick = now;
            f->hall_mag = field_hall_measure(app);
            if(!f->hall_zeroed) {
                f->hall_base = f->hall_mag;
                f->hall_zeroed = true;
            }
        }
        raw_level = (f->hall_mag - f->hall_base) * 2.0f;
    }

    /* a nearby reader field pushes the needle hard (Spectre SENSE) */
    if(eidolon_fd_recent(app, 500)) {
        if(raw_level < 75) raw_level = 75;
    }

    if(raw_level < 0) raw_level = 0;
    if(raw_level > 100) raw_level = 100;
    f->level += (raw_level - f->level) * 0.35f;
    if(f->level > f->peak) f->peak = f->level;
    app->activity_rf = f->level / 100.0f;

    /* geiger clicks, rate follows level */
    if(f->level > 2.0f && now >= f->next_click_tick) {
        eidolon_beep(app, 2000.0f + (float)(furi_hal_random_get() % 900), 0.3f, 12);
        uint32_t interval = (uint32_t)(950.0f - f->level * 8.5f);
        if(interval < 55) interval = 55;
        interval = interval / 2 + (furi_hal_random_get() % interval);
        f->next_click_tick = now + interval;
    }

    /* history strip */
    static uint32_t hist_tick = 0;
    if(now - hist_tick >= 120) {
        hist_tick = now;
        f->history[f->hist_head] = f->level;
        f->hist_head = (f->hist_head + 1) % FIELD_HISTORY;
    }

    /* battery temperature / cold spot */
    if(now - f->last_temp_tick >= 5000) {
        f->last_temp_tick = now;
        f->temp_now = eidolon_battery_temp();
        float drop = f->temp_start - f->temp_now;
        if(drop >= 0.8f && !f->cold_active) {
            f->cold_active = true;
            char tb[16];
            eidolon_fmt_1dp(tb, sizeof(tb), drop);
            eidolon_log_event(app, "FIELD", "cold spot: battery temp down %s C", tb);
            eidolon_surge(app, 0.5f);
        } else if(drop < 0.3f) {
            f->cold_active = false;
        }
    }
}

/* ---------------- draw ---------------- */

static void field_draw(EidolonApp* app, Canvas* canvas) {
    FieldState* f = &app->field;
    char buf[48];
    char num[16];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "FIELD");
    canvas_set_font(canvas, FontSecondary);
    const char* src_name = f->source == FieldSrcSweep ? "SWEEP 300-348" :
                           f->source == FieldSrcBand  ? "BAND" :
                                                         "HALL PIN16";
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, src_name);

    /* gauge: semicircle, center (33,36), r 24 */
    const int cx = 33, cy = 36, r = 24;
    canvas_draw_circle(canvas, cx, cy, r);
    float ang = (float)M_PI * (1.0f - f->level / 100.0f);
    int nx = cx + (int)((float)(r - 3) * cosf(ang));
    int ny = cy - (int)((float)(r - 3) * sinf(ang));
    canvas_draw_line(canvas, cx, cy, nx, ny);
    /* peak tick */
    float pang = (float)M_PI * (1.0f - f->peak / 100.0f);
    int px = cx + (int)((float)r * cosf(pang));
    int py = cy - (int)((float)r * sinf(pang));
    canvas_draw_dot(canvas, px, py);
    canvas_draw_disc(canvas, cx, cy, 2);

    /* state word + readouts on the right */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 66, 22, field_level_name(f->level));

    canvas_set_font(canvas, FontSecondary);
    if(f->source == FieldSrcHall) {
        snprintf(buf, sizeof(buf), "MAG %d  BASE %d", (int)f->hall_mag, (int)f->hall_base);
    } else if(f->source == FieldSrcBand) {
        eidolon_fmt_1dp(num, sizeof(num), f->band_rssi);
        uint32_t mhz = f->band_freq_hz / 1000000;
        uint32_t khz = (f->band_freq_hz % 1000000) / 10000;
        snprintf(buf, sizeof(buf), "%lu.%02luM %s dBm", (unsigned long)mhz, (unsigned long)khz, num);
    } else {
        uint8_t best = 0;
        for(uint8_t i = 1; i < FIELD_SWEEP_STEPS; i++)
            if(f->rssi[i] > f->rssi[best]) best = i;
        eidolon_fmt_1dp(num, sizeof(num), f->rssi[best]);
        snprintf(
            buf,
            sizeof(buf),
            "%luM %s dBm",
            (unsigned long)(field_sweep_freqs[best] / 1000000),
            num);
    }
    canvas_draw_str(canvas, 66, 32, buf);

    /* level bar */
    canvas_draw_frame(canvas, 66, 36, 60, 6);
    int fill = (int)(f->level * 58.0f / 100.0f);
    if(fill > 0) canvas_draw_box(canvas, 67, 37, fill, 4);

    snprintf(buf, sizeof(buf), "PK %d", (int)f->peak);
    canvas_draw_str(canvas, 66, 52, buf);

    /* temperature + cold spot */
    eidolon_fmt_1dp(num, sizeof(num), f->temp_now);
    char tbuf[36];
    char dbuf[12];
    eidolon_fmt_1dp(dbuf, sizeof(dbuf), f->temp_now - f->temp_start);
    snprintf(tbuf, sizeof(tbuf), "T %sC %s", num, dbuf);
    canvas_draw_str(canvas, 96, 52, tbuf);
    if(f->cold_active) canvas_draw_str(canvas, 96, 44, "COLD SPOT");

    /* reader-field lamps */
    bool nfc_r = eidolon_fd.nfc_seen_tick && (furi_get_tick() - eidolon_fd.nfc_seen_tick) < 2000;
    bool rfid_r = eidolon_fd.rfid_seen_tick && (furi_get_tick() - eidolon_fd.rfid_seen_tick) < 2000;
    canvas_draw_str(canvas, 2, 52, "NFC");
    if(nfc_r) canvas_draw_disc(canvas, 24, 49, 3);
    else canvas_draw_circle(canvas, 24, 49, 3);
    canvas_draw_str(canvas, 32, 52, "RFID");
    if(rfid_r) canvas_draw_disc(canvas, 58, 49, 3);
    else canvas_draw_circle(canvas, 58, 49, 3);

    /* history strip */
    for(uint8_t i = 0; i < FIELD_HISTORY; i++) {
        float v = f->history[(f->hist_head + i) % FIELD_HISTORY];
        int h = (int)(v * 7.0f / 100.0f);
        int x = i * 2;
        if(h > 0) canvas_draw_line(canvas, x, 63, x, 63 - h);
    }
}

/* ---------------- input ---------------- */

static bool field_input(EidolonApp* app, InputEvent* event) {
    FieldState* f = &app->field;
    if(event->type == InputTypePress) {
        if(event->key == InputKeyLeft) {
            field_set_source(app, (FieldSource)((f->source + 2) % 3));
            return true;
        }
        if(event->key == InputKeyRight) {
            field_set_source(app, (FieldSource)((f->source + 1) % 3));
            return true;
        }
        if(event->key == InputKeyUp && f->source == FieldSrcBand) {
            uint32_t nf = f->band_freq_hz + 100000;
            if(furi_hal_subghz_is_frequency_valid(nf)) {
                f->band_freq_hz = nf;
                eidolon_radio_tune(app, nf);
            }
            return true;
        }
        if(event->key == InputKeyDown && f->source == FieldSrcBand) {
            uint32_t nf = f->band_freq_hz - 100000;
            if(furi_hal_subghz_is_frequency_valid(nf)) {
                f->band_freq_hz = nf;
                eidolon_radio_tune(app, nf);
            }
            return true;
        }
    }
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        field_zero(app);
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_field = {
    .enter = field_enter,
    .exit = field_exit,
    .tick = field_tick,
    .draw = field_draw,
    .input = field_input,
};
