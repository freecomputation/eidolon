#include "eidolon.h"
#include "eidolon_hw.h"

/* Dark watch: the infrared receiver, watched in the dark. Every burst -
 * remote controls, camera illuminators and flashes, anything modulated
 * in IR - is timestamped into the session log with its protocol,
 * address and command when it decodes. */

static void dark_enter(EidolonApp* app) {
    DarkState* d = &app->dark;
    memset(d, 0, sizeof(*d));
    d->enter_tick = furi_get_tick();
    eidolon_ir_start(app);
    furi_hal_power_insomnia_enter();
    eidolon_log_event(app, "DARK", "dark watch open");
}

static void dark_exit(EidolonApp* app) {
    DarkState* d = &app->dark;
    eidolon_ir_stop(app);
    furi_hal_power_insomnia_exit();
    eidolon_log_event(
        app,
        "DARK",
        "dark watch close: %lu decoded, %lu raw",
        (unsigned long)d->decoded_count,
        (unsigned long)d->raw_count);
}

static void dark_tick(EidolonApp* app) {
    DarkState* d = &app->dark;
    EidolonIrEvent ev;
    while(eidolon_ir_poll(app, &ev)) {
        eidolon_ir_log_event(app, "DARK", &ev);
        d->last_valid = true;
        d->last_decoded = ev.decoded;
        d->last_tick = ev.tick;
        if(ev.decoded) {
            d->decoded_count++;
            snprintf(
                d->last_line1,
                sizeof(d->last_line1),
                "%s A:%lX",
                infrared_get_protocol_name(ev.msg.protocol),
                (unsigned long)ev.msg.address);
            snprintf(
                d->last_line2, sizeof(d->last_line2), "CMD %lX%s", (unsigned long)ev.msg.command, ev.msg.repeat ? " RPT" : "");
        } else {
            d->raw_count++;
            snprintf(d->last_line1, sizeof(d->last_line1), "RAW BURST");
            snprintf(d->last_line2, sizeof(d->last_line2), "%lu TIMINGS", (unsigned long)ev.timings_count);
        }
        eidolon_beep(app, 1500.0f, 0.2f, 30);
        app->activity_ir = 1.0f;
    }
}

/* Counters are session uint32s; drawn clamped so the line fits at any
 * value (unclamped, two full counters ran to 180 px). The log and the
 * close summary keep the true counts. */
static void dark_fmt_counter(char* out, size_t size, uint32_t v) {
    if(v > 99999) {
        snprintf(out, size, "99999+");
    } else {
        snprintf(out, size, "%lu", (unsigned long)v);
    }
}

static void dark_draw(EidolonApp* app, Canvas* canvas) {
    DarkState* d = &app->dark;
    char buf[48];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "DARK WATCH");
    canvas_set_font(canvas, FontSecondary);

    uint32_t elapsed = (furi_get_tick() - d->enter_tick) / 1000;
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(elapsed / 60), (unsigned long)(elapsed % 60));
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, buf);

    canvas_set_font(canvas, FontPrimary);
    snprintf(
        buf,
        sizeof(buf),
        "%lu EVENTS",
        (unsigned long)(d->decoded_count + d->raw_count));
    canvas_draw_str(canvas, 2, 26, buf);
    canvas_set_font(canvas, FontSecondary);
    char dec[12], raw[12];
    dark_fmt_counter(dec, sizeof(dec), d->decoded_count);
    dark_fmt_counter(raw, sizeof(raw), d->raw_count);
    snprintf(buf, sizeof(buf), "DEC %s  RAW %s", dec, raw);
    canvas_draw_str(canvas, 2, 36, buf);

    if(d->last_valid) {
        uint32_t age = (furi_get_tick() - d->last_tick) / 1000;
        canvas_draw_str(canvas, 2, 47, d->last_line1);
        canvas_draw_str(canvas, 2, 55, d->last_line2);
        /* age capped short: at full width it started at x=46 and ran
         * under the command line drawn from x=2 on the same baseline */
        if(age < 60) {
            snprintf(buf, sizeof(buf), "%lus", (unsigned long)age);
        } else if(age < 3600) {
            snprintf(buf, sizeof(buf), "%lum", (unsigned long)(age / 60));
        } else {
            snprintf(buf, sizeof(buf), ">1h");
        }
        canvas_draw_str_aligned(canvas, 127, 55, AlignRight, AlignBottom, buf);
    } else {
        canvas_draw_str(canvas, 2, 47, "no IR energy seen yet");
    }
    canvas_draw_str(canvas, 2, 61, "OK: MARK in log");
}

static bool dark_input(EidolonApp* app, InputEvent* event) {
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        eidolon_log_event(app, "DARK", "manual mark");
        eidolon_cue_confirm(app);
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_dark = {
    .enter = dark_enter,
    .exit = dark_exit,
    .tick = dark_tick,
    .draw = dark_draw,
    .input = dark_input,
};
