#include "eidolon.h"

static const char* const about_lines[] = {
    "EIDOLON - investigation kit",
    "",
    "Every meter in this kit is a",
    "real measurement from the",
    "Flipper's own hardware. The",
    "voice and radar are theater",
    "fed by those measurements",
    "plus the hardware RNG - they",
    "are labeled as such, here and",
    "in the log. Nothing in this",
    "app invents a sensor reading.",
    "",
    "FIELD METER",
    "Sub-GHz RSSI from the CC1101:",
    "an 8-frequency sweep across",
    "300-348 MHz, or one band.",
    "NFC and 125 kHz reader fields",
    "are detected passively and",
    "push the needle. Battery",
    "temperature drop is shown as",
    "a cold spot. OK zeroes the",
    "baseline and the peak.",
    "",
    "HALL MODE (FIELD, pin 16)",
    "Optional SS49E Hall sensor:",
    "OUT -> pin 16, VCC -> pin 9",
    "(3.3V), GND -> pin 11. The",
    "signal is sampled and run",
    "through a Goertzel filter at",
    "50 and 60 Hz: the mains-hum",
    "quantity a K-II meter reads.",
    "",
    "INVESTIGATION LOG",
    "Every event is timestamped",
    "and appended to a session",
    "file in the app data folder",
    "on the SD card.",
    "",
    "CONTROLS",
    "Hold OK anywhere: sound",
    "on/off. Back: leave screen,",
    "then leave the app.",
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
