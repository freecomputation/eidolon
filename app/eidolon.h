#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <storage/storage.h>
#include <datetime/datetime.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct EidolonApp EidolonApp;

typedef enum {
    ScrMenu = 0,
    ScrField,
    ScrBox,
    ScrRem,
    ScrDark,
    ScrVoice,
    ScrRadar,
    ScrFusion,
    ScrAbout,
    ScrCount,
} EidolonScreenId;

typedef struct {
    void (*enter)(EidolonApp* app);
    void (*exit)(EidolonApp* app);
    void (*tick)(EidolonApp* app);
    void (*draw)(EidolonApp* app, Canvas* canvas);
    bool (*input)(EidolonApp* app, InputEvent* event);
} EidolonScreen;

/* ------------------------------------------------------------------ */
/* Logger (eidolon_log.c)                                              */
/* ------------------------------------------------------------------ */

bool eidolon_log_start(EidolonApp* app);
void eidolon_log_stop(EidolonApp* app);
void eidolon_log_event(EidolonApp* app, const char* tag, const char* fmt, ...);

/* Format a float with one decimal without %f (nano printf has no float). */
void eidolon_fmt_1dp(char* buf, size_t size, float value);

/* ------------------------------------------------------------------ */
/* Word engine (eidolon_words.c)                                       */
/* ------------------------------------------------------------------ */

#define EIDOLON_WORD_MAX 24
#define EIDOLON_WORD_HISTORY 3

typedef struct {
    char history[EIDOLON_WORD_HISTORY][EIDOLON_WORD_MAX];
    uint8_t history_count;
    char current[EIDOLON_WORD_MAX];
    uint8_t shown;
    bool spelling;
    uint32_t next_letter_tick;
    uint32_t done_tick;
    char source[12];
    uint8_t pending; /* queued follow-up words (multi-word answers) */
} EidolonWords;

void eidolon_words_init(EidolonApp* app);
void eidolon_words_trigger(EidolonApp* app, const char* source);
void eidolon_words_tick(EidolonApp* app);
const char* eidolon_words_pick(void);

/* ------------------------------------------------------------------ */
/* Per-screen state                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    FieldSrcSweep = 0,
    FieldSrcBand,
    FieldSrcHall,
} FieldSource;

#define FIELD_SWEEP_STEPS 8
#define FIELD_HISTORY 64

typedef struct {
    FieldSource source;
    uint8_t step;
    float rssi[FIELD_SWEEP_STEPS];
    float baseline[FIELD_SWEEP_STEPS];
    bool baseline_set;
    uint32_t band_freq_hz;
    float band_rssi;
    float band_baseline;
    float level; /* 0..100 smoothed */
    float peak;
    float history[FIELD_HISTORY];
    uint8_t hist_head;
    uint32_t last_sample_tick;
    uint32_t next_click_tick;
    float temp_start;
    float temp_now;
    bool temp_valid;
    bool cold_active;
    uint32_t last_temp_tick;
    float hall_mag;
    float hall_base;
    bool hall_zeroed;
    uint32_t hall_last_tick;
} FieldState;

typedef struct {
    uint8_t scroll; /* first visible line */
} AboutState;

/* ------------------------------------------------------------------ */
/* App                                                                 */
/* ------------------------------------------------------------------ */

struct EidolonApp {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    Storage* storage;

    EidolonScreenId screen;
    uint8_t menu_index;
    bool running;
    bool sound_on;
    bool suppress_ok_release;

    /* session logger */
    File* log_file;
    bool log_open;
    uint32_t start_tick;
    char session_name[48];
    char last_event[64];

    /* beep scheduler (core) */
    bool speaker_held;
    bool beep_active;
    uint32_t beep_stop_tick;
    bool mirror_active; /* spirit box owns gpio_speaker */

    /* surge feedback */
    uint32_t flash_until;

    /* shared activity levels 0..1, decayed by core tick */
    float activity_rf;
    float activity_rem;
    float activity_ir;

    /* shared sensor snapshots for voice/radar/fusion weighting */
    uint32_t ir_events_total;
    uint32_t ir_last_tick;
    bool field_present;
    uint32_t field_present_tick;

    EidolonWords words;
    FieldState field;
    AboutState about;
};

/* Core feedback helpers (eidolon.c) */
void eidolon_beep(EidolonApp* app, float freq_hz, float volume, uint32_t duration_ms);
void eidolon_tone_start(EidolonApp* app, float freq_hz, float volume);
void eidolon_tone_stop(EidolonApp* app);
void eidolon_led(EidolonApp* app, uint8_t r, uint8_t g, uint8_t b);
void eidolon_led_off(EidolonApp* app);
void eidolon_surge(EidolonApp* app, float level01);
bool eidolon_flash_active(EidolonApp* app);
float eidolon_activity(EidolonApp* app); /* combined 0..1 */
