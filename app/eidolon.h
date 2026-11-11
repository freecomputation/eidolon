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

#define BOX_STEPS 64

typedef struct {
    uint8_t band; /* 0: 300-348, 1: 387-464, 2: 779-928 MHz */
    uint32_t f_start;
    uint32_t f_stop;
    uint8_t step;
    uint32_t dwell_ms;
    uint32_t step_deadline;
    bool sampled;
    float ribbon[BOX_STEPS]; /* RSSI per step, dBm */
    float floor_dbm;
    bool floor_valid;
    float rssi_now;
    bool muted;
    int16_t last_above_step;
    uint8_t lock_hits;
    uint32_t lock_cooldown_until;
} BoxState;

typedef struct {
    uint16_t jitter;
    uint16_t base;
    bool zeroed;
    uint8_t sensitivity; /* 1..10 */
    uint8_t volume; /* 0..10 */
    bool alert;
    uint8_t over_ticks;
    uint32_t strobe_tick;
    bool strobe_on;
} RemState;

typedef struct {
    uint32_t decoded_count;
    uint32_t raw_count;
    bool last_valid;
    bool last_decoded;
    char last_line1[32];
    char last_line2[32];
    uint32_t last_tick;
    uint32_t enter_tick;
} DarkState;

typedef struct {
    bool asking;
    uint32_t answer_at;
    uint32_t next_auto_tick;
} VoiceState;

#define RADAR_ECHOES 6

typedef struct {
    bool active;
    float angle; /* bearing, degrees */
    float range; /* 0..100 */
    float speed; /* range units per second, inward */
    bool pinged;
} RadarEcho;

typedef struct {
    float beam_deg;
    RadarEcho echoes[RADAR_ECHOES];
    uint32_t last_tick;
    char answer[8];
    uint32_t answer_until;
} RadarState;

typedef struct {
    uint8_t radio_step;
    float rf_rssi;
    float rf_floor;
    uint16_t rem_jitter;
    uint16_t rem_base;
    bool rem_zeroed;
    float presence;
    float peak_presence;
    uint32_t last_summary_tick;
    uint32_t last_temp_tick;
    float temp_start;
    float temp_now;
    bool contact;
} FusionState;

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
    BoxState box;
    RemState rem;
    DarkState dark;
    VoiceState voice;
    RadarState radar;
    FusionState fusion;
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
