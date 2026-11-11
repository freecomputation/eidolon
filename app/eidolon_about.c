#include "eidolon.h"

static const char* const about_lines[] = {
    "EIDOLON - apparition kit",
    "",
    "THE HONEST PART",
    "Meters here are real:",
    "RF power, ADC jitter,",
    "IR bursts, fields and",
    "battery temperature",
    "are measured, never",
    "faked. The VOICE and",
    "RADAR are theater fed",
    "by those measurements",
    "and the hardware RNG.",
    "Word picks are random;",
    "what is real is WHEN",
    "surges trigger them.",
    "",
    "FIELD METER",
    "CC1101 RSSI: 8-step",
    "sweep 300-348 MHz or",
    "one band (U/D steps).",
    "L/R: sweep, band, or",
    "HALL. OK: zero base",
    "and peak. Geiger",
    "clicks follow level.",
    "Reader fields (NFC,",
    "125 kHz) push the",
    "needle. Battery temp",
    "drop = COLD SPOT.",
    "",
    "HALL (pin 16)",
    "SS49E Hall sensor:",
    "OUT pin16, VCC pin9",
    "(3.3V), GND pin11.",
    "Goertzel filter at",
    "50/60 Hz mains hum:",
    "the K-II quantity.",
    "",
    "SPIRIT BOX",
    "Real Sub-GHz sweep;",
    "the raw demodulated",
    "RX bitstream drives",
    "the speaker pin, so",
    "the static is real",
    "RF. Ribbon = noise",
    "floor per step. A",
    "carrier 14 dB over",
    "floor = lock-on, and",
    "cues a word. U/D:",
    "dwell. L/R: band.",
    "OK: mute.",
    "",
    "REM POD (pin 15)",
    "Bare copper wire,",
    "about 15 cm, on pin",
    "15. A floating ADC",
    "node couples to the",
    "mains field; bodies",
    "change it. Signal =",
    "peak-peak jitter of",
    "24 raw samples.",
    "L/R sens, U/D vol,",
    "OK zero.",
    "",
    "DARK WATCH",
    "The IR receiver,",
    "logged: protocol,",
    "address, command",
    "when a burst",
    "decodes, raw timing",
    "count when not.",
    "OK: manual mark.",
    "",
    "VOICE",
    "Words on measured",
    "surges, spelled out",
    "with a blip each.",
    "OK: ask, and wait.",
    "Last 3 words kept.",
    "Words are logged.",
    "",
    "RADAR",
    "Sweep scope. Echoes",
    "weighted by real",
    "activity. Beam pings",
    "crossing an echo.",
    "OK: yes or no.",
    "",
    "FUSION WATCH",
    "All sensors at once:",
    "RF, REM, IR, reader",
    "field, cold trend -",
    "one presence level,",
    "one timeline in the",
    "log. OK re-zeroes.",
    "",
    "LOG",
    "Session file in the",
    "app data folder on",
    "the SD card: every",
    "event timestamped",
    "with clock + uptime.",
    "",
    "CONTROLS",
    "Hold OK: sound on",
    "or off, anywhere.",
    "Back: leave screen,",
    "then the app.",
};

#define ABOUT_LINE_COUNT (sizeof(about_lines) / sizeof(about_lines[0]))
#define ABOUT_VISIBLE 6

static void about_enter(EidolonApp* app) {
    app->about.scroll = 0;
}

static void about_draw(EidolonApp* app, Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "ABOUT");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < ABOUT_VISIBLE; i++) {
        uint8_t idx = app->about.scroll + i;
        if(idx >= ABOUT_LINE_COUNT) break;
        canvas_draw_str(canvas, 2, 22 + i * 7, about_lines[idx]);
    }
    if(ABOUT_LINE_COUNT > ABOUT_VISIBLE) {
        uint8_t max_scroll = ABOUT_LINE_COUNT - ABOUT_VISIBLE;
        int bar_h = 40 * ABOUT_VISIBLE / ABOUT_LINE_COUNT;
        int bar_y = 14 + (40 - bar_h) * app->about.scroll / (max_scroll ? max_scroll : 1);
        canvas_draw_box(canvas, 126, bar_y, 2, bar_h);
    }
}

static bool about_input(EidolonApp* app, InputEvent* event) {
    if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        uint8_t max_scroll = ABOUT_LINE_COUNT > ABOUT_VISIBLE ? ABOUT_LINE_COUNT - ABOUT_VISIBLE : 0;
        if(event->key == InputKeyDown && app->about.scroll < max_scroll) {
            app->about.scroll++;
            return true;
        }
        if(event->key == InputKeyUp && app->about.scroll > 0) {
            app->about.scroll--;
            return true;
        }
    }
    return false;
}

const EidolonScreen eidolon_screen_about = {
    .enter = about_enter,
    .exit = NULL,
    .tick = NULL,
    .draw = about_draw,
    .input = about_input,
};
