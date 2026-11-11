#include "eidolon.h"

extern const EidolonScreen eidolon_screen_field;
extern const EidolonScreen eidolon_screen_box;
extern const EidolonScreen eidolon_screen_rem;
extern const EidolonScreen eidolon_screen_dark;
extern const EidolonScreen eidolon_screen_voice;
extern const EidolonScreen eidolon_screen_radar;
extern const EidolonScreen eidolon_screen_fusion;
extern const EidolonScreen eidolon_screen_about;

static const EidolonScreen* const eidolon_screens[ScrCount] = {
    [ScrMenu] = NULL,
    [ScrField] = &eidolon_screen_field,
    [ScrBox] = &eidolon_screen_box,
    [ScrRem] = &eidolon_screen_rem,
    [ScrDark] = &eidolon_screen_dark,
    [ScrVoice] = &eidolon_screen_voice,
    [ScrRadar] = &eidolon_screen_radar,
    [ScrFusion] = &eidolon_screen_fusion,
    [ScrAbout] = &eidolon_screen_about,
};

typedef struct {
    const char* name;
    EidolonScreenId id;
} EidolonMenuEntry;

static const EidolonMenuEntry eidolon_menu[] = {
    {"Field Meter", ScrField},
    {"Spirit Box", ScrBox},
    {"REM Pod", ScrRem},
    {"Dark Watch", ScrDark},
    {"Voice", ScrVoice},
    {"Radar", ScrRadar},
    {"Fusion Watch", ScrFusion},
    {"About", ScrAbout},
};

#define MENU_COUNT ((int)(sizeof(eidolon_menu) / sizeof(eidolon_menu[0])))
#define MENU_VISIBLE 5

/* ------------------------------------------------------------------ */
/* Feedback helpers                                                    */
/* ------------------------------------------------------------------ */

static uint32_t speaker_idle_since;

static bool eidolon_speaker_acquire(EidolonApp* app) {
    if(app->speaker_held) return true;
    if(furi_hal_speaker_acquire(500)) {
        app->speaker_held = true;
        return true;
    }
    return false;
}

void eidolon_tone_start(EidolonApp* app, float freq_hz, float volume) {
    if(app->mirror_active) return;
    if(!eidolon_speaker_acquire(app)) return;
    furi_hal_speaker_start(freq_hz, volume);
    app->tone_active = true;
    app->beep_active = false;
}

void eidolon_tone_stop(EidolonApp* app) {
    if(!app->tone_active) return;
    furi_hal_speaker_stop();
    app->tone_active = false;
    speaker_idle_since = furi_get_tick();
}

void eidolon_beep(EidolonApp* app, float freq_hz, float volume, uint32_t duration_ms) {
    if(!app->sound_on || app->mirror_active || app->tone_active) return;
    if(!eidolon_speaker_acquire(app)) return;
    furi_hal_speaker_start(freq_hz, volume);
    app->beep_active = true;
    app->beep_stop_tick = furi_get_tick() + duration_ms;
}

void eidolon_speaker_yield(EidolonApp* app) {
    if(app->mirror_active || app->tone_active) return;
    if(app->beep_active) {
        furi_hal_speaker_stop();
        app->beep_active = false;
    }
    if(app->speaker_held) {
        furi_hal_speaker_release();
        app->speaker_held = false;
        speaker_idle_since = furi_get_tick();
    }
}

bool eidolon_flash_active(EidolonApp* app) {
    return furi_get_tick() < app->flash_until;
}

float eidolon_activity(EidolonApp* app) {
    float ir = 0;
    if(app->ir_events_total > 0) {
        uint32_t age = furi_get_tick() - app->ir_last_tick;
        if(age < 8000) ir = 1.0f - (float)age / 8000.0f;
    }
    float a = app->activity_rf;
    if(app->activity_rem > a) a = app->activity_rem;
    if(ir > a) a = ir;
    if(app->field_present && a < 0.6f) a = 0.6f;
    return a;
}

/* ------------------------------------------------------------------ */
/* Screen management                                                   */
/* ------------------------------------------------------------------ */

static void eidolon_enter_screen(EidolonApp* app, EidolonScreenId id) {
    const EidolonScreen* cur = eidolon_screens[app->screen];
    if(cur && cur->exit) cur->exit(app);
    app->screen = id;
    const EidolonScreen* next = eidolon_screens[id];
    if(next && next->enter) next->enter(app);
    view_port_update(app->view_port);
}

static void eidolon_core_tick(EidolonApp* app) {
    uint32_t now = furi_get_tick();
    /* activity decay (~3 s time constant at loop rate) */
    app->activity_rf *= 0.995f;
    app->activity_rem *= 0.995f;
    /* beep stop */
    if(app->beep_active && now >= app->beep_stop_tick) {
        furi_hal_speaker_stop();
        app->beep_active = false;
        speaker_idle_since = now;
    }
    /* release an idle speaker so system sounds are not starved */
    if(app->speaker_held && !app->beep_active && !app->tone_active &&
       now - speaker_idle_since > 800) {
        furi_hal_speaker_release();
        app->speaker_held = false;
    }
    eidolon_feedback_tick(app);
    eidolon_words_tick(app);
}

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

static void eidolon_menu_draw(EidolonApp* app, Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "EIDOLON");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, app->sound_on ? "SND ON" : "SND OFF");

    uint8_t offset = 0;
    if(app->menu_index >= MENU_VISIBLE) offset = app->menu_index - MENU_VISIBLE + 1;
    for(uint8_t i = 0; i < MENU_VISIBLE && (offset + i) < MENU_COUNT; i++) {
        uint8_t idx = offset + i;
        int y = 21 + i * 9;
        if(idx == app->menu_index) {
            canvas_draw_box(canvas, 0, y - 7, 128, 9);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 6, y, eidolon_menu[idx].name);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 6, y, eidolon_menu[idx].name);
        }
    }
    canvas_draw_str(canvas, 2, 63, app->last_event);
}

static bool eidolon_menu_input(EidolonApp* app, InputEvent* event) {
    if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        if(event->key == InputKeyDown) {
            if(app->menu_index + 1 < MENU_COUNT) {
                app->menu_index++;
                eidolon_cue_click(app);
            }
            return true;
        }
        if(event->key == InputKeyUp) {
            if(app->menu_index > 0) {
                app->menu_index--;
                eidolon_cue_click(app);
            }
            return true;
        }
    }
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        eidolon_cue_confirm(app);
        eidolon_enter_screen(app, eidolon_menu[app->menu_index].id);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* ViewPort callbacks                                                  */
/* ------------------------------------------------------------------ */

static void eidolon_draw_callback(Canvas* canvas, void* context) {
    EidolonApp* app = context;
    canvas_clear(canvas);
    bool flash = eidolon_flash_active(app);
    if(flash) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, 128, 64);
        canvas_set_color(canvas, ColorWhite);
    }
    if(app->screen == ScrMenu) {
        eidolon_menu_draw(app, canvas);
    } else {
        const EidolonScreen* scr = eidolon_screens[app->screen];
        if(scr && scr->draw) scr->draw(app, canvas);
    }
    if(flash) canvas_set_color(canvas, ColorBlack);
}

static void eidolon_input_callback(InputEvent* event, void* context) {
    EidolonApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

static void eidolon_handle_input(EidolonApp* app, InputEvent* event) {
    /* hold OK anywhere: global feedback toggle (cue sound + vibration;
     * the LED channel and the canvas flash always stay) */
    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        app->sound_on = !app->sound_on;
        app->suppress_ok_release = true;
        eidolon_log_event(app, "APP", "feedback %s", app->sound_on ? "on" : "off");
        if(app->sound_on) {
            eidolon_cue_confirm(app);
        } else {
            eidolon_feedback_all_stop(app);
        }
        return;
    }
    if(event->type == InputTypeRelease && event->key == InputKeyOk && app->suppress_ok_release) {
        app->suppress_ok_release = false;
        return;
    }

    bool consumed = false;
    if(app->screen == ScrMenu) {
        consumed = eidolon_menu_input(app, event);
    } else {
        const EidolonScreen* scr = eidolon_screens[app->screen];
        if(scr && scr->input) consumed = scr->input(app, event);
    }
    if(consumed) return;

    /* default navigation: Back leaves the screen, then the app */
    if(event->type == InputTypeRelease && event->key == InputKeyBack) {
        if(app->screen != ScrMenu) {
            eidolon_enter_screen(app, ScrMenu);
        } else {
            app->running = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int32_t eidolon_main(void* p) {
    UNUSED(p);
    EidolonApp* app = malloc(sizeof(EidolonApp));
    memset(app, 0, sizeof(EidolonApp));
    app->running = true;
    app->sound_on = true;
    app->screen = ScrMenu;

    app->storage = furi_record_open(RECORD_STORAGE);
    eidolon_feedback_init(app);
    eidolon_log_start(app);
    eidolon_words_init(app);

    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, eidolon_draw_callback, app);
    view_port_input_callback_set(app->view_port, eidolon_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    while(app->running) {
        if(furi_message_queue_get(app->event_queue, &event, 25) == FuriStatusOk) {
            eidolon_handle_input(app, &event);
        }
        eidolon_core_tick(app);
        const EidolonScreen* scr = eidolon_screens[app->screen];
        if(scr && scr->tick) scr->tick(app);
        view_port_update(app->view_port);
    }

    /* leave the active screen cleanly (releases radios, ADC, insomnia) */
    const EidolonScreen* cur = eidolon_screens[app->screen];
    if(cur && cur->exit) cur->exit(app);
    eidolon_led_off(app);
    if(app->speaker_held) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker_held = false;
    }

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);

    eidolon_log_stop(app);
    eidolon_feedback_deinit(app);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
