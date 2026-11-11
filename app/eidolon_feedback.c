#include "eidolon.h"

/* Feedback engine: the notification service owns the vibration motor,
 * the RGB LED and every discrete cue sound, so an event is felt and
 * seen as well as heard - the standard Spectre sets on this hardware.
 *
 * Ownership split:
 * - Notification service (here): vibro, LED, cue sounds. Cues are
 *   composed sequences (notes, delays, vibro pulses, LED colors) and
 *   are all static const, because notification_message is async and
 *   reads the sequence after the call returns.
 * - Direct HAL speaker (eidolon.c): only continuous or high-rate
 *   instrument audio - Geiger clicks, the REM alert tone, word
 *   syllable blips, and the spirit box's hardware mirror pin.
 *
 * Fallback rule, deliberate per event: while the speaker is busy (the
 * spirit mirror is live, or the REM tone is sounding), a cue is sent
 * in its vibro + LED variant instead of its full variant. An event is
 * never silently dropped. The hold-OK toggle governs cue sound and
 * vibration together; with it off, cues still fire their LED variant.
 * The user's own system volume / vibro settings are never overridden
 * (no Force* messages are used). */

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */
/* ------------------------------------------------------------------ */

#define FB_DELAY(name, ms) \
    static const NotificationMessage name = { \
        .type = NotificationMessageTypeDelay, .data = {.delay = {.length = (ms)}}}

FB_DELAY(fb_delay_40, 40);
FB_DELAY(fb_delay_80, 80);
FB_DELAY(fb_delay_90, 90);
FB_DELAY(fb_delay_120, 120);
FB_DELAY(fb_delay_150, 150);
FB_DELAY(fb_delay_200, 200);
FB_DELAY(fb_delay_400, 400);

#define FB_LED(name, rv, gv, bv) \
    static const NotificationMessage name##_r = { \
        .type = NotificationMessageTypeLedRed, .data = {.led = {.value = (rv)}}}; \
    static const NotificationMessage name##_g = { \
        .type = NotificationMessageTypeLedGreen, .data = {.led = {.value = (gv)}}}; \
    static const NotificationMessage name##_b = { \
        .type = NotificationMessageTypeLedBlue, .data = {.led = {.value = (bv)}}}

FB_LED(fb_led_green_dim, 0, 70, 0);
FB_LED(fb_led_green_mid, 0, 160, 0);
FB_LED(fb_led_green_white, 170, 255, 170);
FB_LED(fb_led_blue_cold, 0, 80, 220);
FB_LED(fb_led_white_dim, 70, 70, 70);
FB_LED(fb_led_red_dim, 150, 0, 0);

#define FB_LED_SET(name) &name##_r, &name##_g, &name##_b
#define FB_LED_OFF &message_red_0, &message_green_0, &message_blue_0

static const NotificationSequence fb_seq_noop = {&message_do_not_reset, NULL};

static const NotificationSequence fb_seq_led_off = {
    &message_blink_stop,
    FB_LED_OFF,
    NULL};

static const NotificationSequence fb_seq_all_stop = {
    &message_sound_off,
    &message_vibro_off,
    &message_blink_stop,
    FB_LED_OFF,
    NULL};

/* ------------------------------------------------------------------ */
/* Cue sequences: full (sound + vibro + LED), silent (vibro + LED),    */
/* led (LED only, toggle off)                                          */
/* ------------------------------------------------------------------ */

/* click: menu moves, source switches - a 25 ms blip, nothing else */
static const NotificationSequence fb_click_full = {
    &message_note_e7,
    &message_delay_25,
    &message_sound_off,
    NULL};

/* confirm: OK actions (zero, mark, open screen, toggle on) */
static const NotificationSequence fb_confirm_full = {
    &message_vibro_on,
    &message_note_e6,
    &fb_delay_40,
    &message_vibro_off,
    &message_delay_25,
    &message_sound_off,
    NULL};
static const NotificationSequence fb_confirm_silent = {
    &message_vibro_on,
    &fb_delay_40,
    &message_vibro_off,
    NULL};

/* lock-on: rising arpeggio C6 E6 G6 C7 over a green flash */
static const NotificationSequence fb_lockon_full = {
    FB_LED_SET(fb_led_green_mid),
    &message_vibro_on,
    &message_note_c6,
    &fb_delay_80,
    &message_note_e6,
    &fb_delay_80,
    &message_note_g6,
    &message_vibro_off,
    &fb_delay_80,
    &message_note_c7,
    &fb_delay_120,
    &message_sound_off,
    &message_delay_100,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_lockon_silent = {
    FB_LED_SET(fb_led_green_mid),
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_100,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_lockon_led = {
    FB_LED_SET(fb_led_green_mid),
    &message_delay_250,
    FB_LED_OFF,
    NULL};

/* cold spot: descending A5 -> E5, blue flash, double pulse */
static const NotificationSequence fb_cold_full = {
    FB_LED_SET(fb_led_blue_cold),
    &message_vibro_on,
    &message_note_a5,
    &fb_delay_120,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_note_e5,
    &fb_delay_150,
    &message_vibro_off,
    &message_sound_off,
    &message_delay_100,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_cold_silent = {
    FB_LED_SET(fb_led_blue_cold),
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_100,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_cold_led = {
    FB_LED_SET(fb_led_blue_cold),
    &message_delay_250,
    FB_LED_OFF,
    NULL};

/* word: a word finished spelling - soft E6 -> A6 chime, white flash */
static const NotificationSequence fb_word_full = {
    FB_LED_SET(fb_led_white_dim),
    &message_vibro_on,
    &message_note_e6,
    &fb_delay_90,
    &message_vibro_off,
    &message_note_a6,
    &fb_delay_150,
    &message_sound_off,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_word_silent = {
    FB_LED_SET(fb_led_white_dim),
    &message_vibro_on,
    &fb_delay_90,
    &message_vibro_off,
    &message_delay_50,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_word_led = {
    FB_LED_SET(fb_led_white_dim),
    &fb_delay_200,
    FB_LED_OFF,
    NULL};

/* radar answer: YES rises G5 -> C6 (green), NO falls C6 -> G5 (red) */
static const NotificationSequence fb_yes_full = {
    FB_LED_SET(fb_led_green_dim),
    &message_vibro_on,
    &message_note_g5,
    &fb_delay_90,
    &message_note_c6,
    &message_vibro_off,
    &fb_delay_150,
    &message_sound_off,
    &message_delay_50,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_yes_silent = {
    FB_LED_SET(fb_led_green_dim),
    &message_vibro_on,
    &fb_delay_120,
    &message_vibro_off,
    &fb_delay_80,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_yes_led = {
    FB_LED_SET(fb_led_green_dim),
    &fb_delay_200,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_no_full = {
    FB_LED_SET(fb_led_red_dim),
    &message_vibro_on,
    &message_note_c6,
    &fb_delay_90,
    &message_note_g5,
    &message_vibro_off,
    &fb_delay_150,
    &message_sound_off,
    &message_delay_50,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_no_silent = {
    FB_LED_SET(fb_led_red_dim),
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &fb_delay_40,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_50,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_no_led = {
    FB_LED_SET(fb_led_red_dim),
    &fb_delay_200,
    FB_LED_OFF,
    NULL};

/* surge tiers, scaled by event strength */
static const NotificationSequence fb_surge_weak = {
    FB_LED_SET(fb_led_green_dim),
    &message_vibro_on,
    &fb_delay_40,
    &message_vibro_off,
    &message_delay_50,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_weak_led = {
    FB_LED_SET(fb_led_green_dim),
    &message_delay_100,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_mid_full = {
    FB_LED_SET(fb_led_green_mid),
    &message_vibro_on,
    &message_note_a5,
    &fb_delay_90,
    &message_vibro_off,
    &message_sound_off,
    &fb_delay_80,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_mid_silent = {
    FB_LED_SET(fb_led_green_mid),
    &message_vibro_on,
    &fb_delay_90,
    &message_vibro_off,
    &fb_delay_80,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_mid_led = {
    FB_LED_SET(fb_led_green_mid),
    &fb_delay_200,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_strong_full = {
    FB_LED_SET(fb_led_green_white),
    &message_vibro_on,
    &message_note_c6,
    &fb_delay_80,
    &message_note_g6,
    &fb_delay_80,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &fb_delay_90,
    &message_vibro_off,
    &message_sound_off,
    &fb_delay_120,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_strong_silent = {
    FB_LED_SET(fb_led_green_white),
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &fb_delay_120,
    FB_LED_OFF,
    NULL};
static const NotificationSequence fb_surge_strong_led = {
    FB_LED_SET(fb_led_green_white),
    &message_delay_250,
    FB_LED_OFF,
    NULL};

/* REM alert: red LED blink runs in the service until stopped, vibro
 * pulses on start and every 2 s while the alert lasts */
static const NotificationMessage fb_blink_color_red = {
    .type = NotificationMessageTypeLedBlinkColor,
    .data = {.led_blink = {.on_time = 0, .period = 0, .color = LightRed}}};
static const NotificationMessage fb_blink_start_red = {
    .type = NotificationMessageTypeLedBlinkStart,
    .data = {.led_blink = {.on_time = 120, .period = 260, .color = LightRed}}};
static const NotificationSequence fb_seq_blink_red = {
    &fb_blink_color_red,
    &fb_blink_start_red,
    NULL};
static const NotificationSequence fb_seq_alert_vibro = {
    &message_vibro_on,
    &fb_delay_400,
    &message_vibro_off,
    NULL};
static const NotificationSequence fb_seq_alert_repulse = {
    &message_vibro_on,
    &fb_delay_150,
    &message_vibro_off,
    NULL};
static const NotificationSequence fb_seq_alert_stop = {
    &message_blink_stop,
    &message_vibro_off,
    FB_LED_OFF,
    NULL};

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void fb_send(EidolonApp* app, const NotificationSequence* sequence) {
    if(app->notifications && sequence) {
        notification_message(app->notifications, sequence);
    }
}

/* One shared throttle: cues of any class arriving inside the gap are
 * coalesced, so a burst of events feels like one event, not a rattle. */
static bool fb_gate(EidolonApp* app, uint32_t min_gap_ms) {
    uint32_t now = furi_get_tick();
    if(now - app->fb_last_cue < min_gap_ms) return false;
    app->fb_last_cue = now;
    return true;
}

static void fb_dispatch(
    EidolonApp* app,
    const NotificationSequence* full,
    const NotificationSequence* silent,
    const NotificationSequence* led,
    uint32_t min_gap_ms) {
    if(!fb_gate(app, min_gap_ms)) return;
    if(!app->sound_on) {
        fb_send(app, led);
        return;
    }
    if(app->mirror_active || app->tone_active) {
        /* speaker busy by design: vibro + LED carry the event */
        fb_send(app, silent);
        return;
    }
    eidolon_speaker_yield(app);
    fb_send(app, full);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void eidolon_feedback_init(EidolonApp* app) {
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
}

void eidolon_feedback_deinit(EidolonApp* app) {
    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
        app->notifications = NULL;
    }
}

void eidolon_feedback_tick(EidolonApp* app) {
    if(app->fb_alert && app->sound_on && furi_get_tick() >= app->fb_alert_pulse) {
        app->fb_alert_pulse = furi_get_tick() + 2000;
        fb_send(app, &fb_seq_alert_repulse);
    }
}

void eidolon_feedback_all_stop(EidolonApp* app) {
    app->fb_alert = false;
    fb_send(app, &fb_seq_all_stop);
}

void eidolon_led_off(EidolonApp* app) {
    app->fb_alert = false;
    fb_send(app, &fb_seq_led_off);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

void eidolon_surge(EidolonApp* app, float level01) {
    if(level01 < 0) level01 = 0;
    if(level01 > 1) level01 = 1;
    app->flash_until = furi_get_tick() + 150 + (uint32_t)(level01 * 250.0f);
    if(level01 < 0.35f) {
        fb_dispatch(app, &fb_surge_weak, &fb_surge_weak, &fb_surge_weak_led, 250);
    } else if(level01 < 0.7f) {
        fb_dispatch(app, &fb_surge_mid_full, &fb_surge_mid_silent, &fb_surge_mid_led, 250);
    } else {
        fb_dispatch(
            app, &fb_surge_strong_full, &fb_surge_strong_silent, &fb_surge_strong_led, 250);
    }
}

void eidolon_cue_click(EidolonApp* app) {
    fb_dispatch(app, &fb_click_full, &fb_seq_noop, &fb_seq_noop, 70);
}

void eidolon_cue_confirm(EidolonApp* app) {
    fb_dispatch(app, &fb_confirm_full, &fb_confirm_silent, &fb_seq_noop, 150);
}

void eidolon_cue_lockon(EidolonApp* app) {
    app->flash_until = furi_get_tick() + 300;
    fb_dispatch(app, &fb_lockon_full, &fb_lockon_silent, &fb_lockon_led, 250);
}

void eidolon_cue_cold(EidolonApp* app) {
    app->flash_until = furi_get_tick() + 300;
    fb_dispatch(app, &fb_cold_full, &fb_cold_silent, &fb_cold_led, 250);
}

void eidolon_cue_word(EidolonApp* app) {
    fb_dispatch(app, &fb_word_full, &fb_word_silent, &fb_word_led, 250);
}

void eidolon_cue_answer(EidolonApp* app, bool yes) {
    if(yes) {
        fb_dispatch(app, &fb_yes_full, &fb_yes_silent, &fb_yes_led, 250);
    } else {
        fb_dispatch(app, &fb_no_full, &fb_no_silent, &fb_no_led, 250);
    }
}

void eidolon_alert_feedback_start(EidolonApp* app) {
    app->fb_alert = true;
    app->fb_alert_pulse = furi_get_tick() + 2000;
    app->flash_until = furi_get_tick() + 300;
    fb_send(app, &fb_seq_blink_red); /* LED blink is not toggle-gated */
    if(app->sound_on) fb_send(app, &fb_seq_alert_vibro);
}

void eidolon_alert_feedback_stop(EidolonApp* app) {
    app->fb_alert = false;
    fb_send(app, &fb_seq_alert_stop);
}
