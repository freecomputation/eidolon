#include "eidolon.h"

/* Radar: a sweeping scope. Echo appearance is weighted by the kit's
 * measured activity (field / REM / IR) plus the hardware RNG - theater
 * driven by real instruments, same honesty rule as the voice. The
 * nearest echo's range and bearing are read out, the beam pings as it
 * crosses an echo, and OK asks a yes/no question. */

#define RADAR_CX 36
#define RADAR_CY 34
#define RADAR_R 27

static float radar_ang_diff(float a, float b) {
    float d = a - b;
    while(d > 180.0f) d -= 360.0f;
    while(d < -180.0f) d += 360.0f;
    return d < 0 ? -d : d;
}

static void radar_enter(EidolonApp* app) {
    RadarState* r = &app->radar;
    memset(r, 0, sizeof(*r));
    r->last_tick = furi_get_tick();
    eidolon_log_event(app, "RADAR", "radar open");
}

static void radar_exit(EidolonApp* app) {
    eidolon_log_event(app, "RADAR", "radar close");
}

static void radar_tick(EidolonApp* app) {
    RadarState* r = &app->radar;
    uint32_t now = furi_get_tick();
    uint32_t dt = now - r->last_tick;
    if(dt > 200) dt = 200;
    r->last_tick = now;
    float act = eidolon_activity(app);

    float beam_step = 45.0f * (float)dt / 1000.0f;
    r->beam_deg += beam_step;
    if(r->beam_deg >= 360.0f) r->beam_deg -= 360.0f;

    /* spawn echoes, weighted by measured activity */
    float rate = 0.15f + act * 1.1f; /* per second */
    float p = rate * (float)dt / 1000.0f;
    if((float)(furi_hal_random_get() % 100000) < p * 100000.0f) {
        for(uint8_t i = 0; i < RADAR_ECHOES; i++) {
            if(!r->echoes[i].active) {
                RadarEcho* e = &r->echoes[i];
                e->active = true;
                e->angle = (float)(furi_hal_random_get() % 360);
                e->range = 82.0f + (float)(furi_hal_random_get() % 18);
                e->speed = 2.0f + (float)(furi_hal_random_get() % 40) / 10.0f;
                e->pinged = false;
                eidolon_log_event(
                    app,
                    "RADAR",
                    "echo at brg %d rng %d",
                    (int)e->angle,
                    (int)e->range);
                break;
            }
        }
    }

    for(uint8_t i = 0; i < RADAR_ECHOES; i++) {
        RadarEcho* e = &r->echoes[i];
        if(!e->active) continue;
        e->range -= e->speed * (float)dt / 1000.0f;
        if(e->range < 6.0f) {
            e->active = false;
            continue;
        }
        float diff = radar_ang_diff(r->beam_deg, e->angle);
        if(diff < beam_step + 4.0f) {
            if(!e->pinged) {
                e->pinged = true;
                eidolon_beep(app, 1500.0f - e->range * 10.0f, 0.3f, 50);
            }
        } else if(diff > 40.0f) {
            e->pinged = false;
        }
    }
}

static RadarEcho* radar_nearest(EidolonApp* app) {
    RadarState* r = &app->radar;
    RadarEcho* best = NULL;
    for(uint8_t i = 0; i < RADAR_ECHOES; i++) {
        if(!r->echoes[i].active) continue;
        if(!best || r->echoes[i].range < best->range) best = &r->echoes[i];
    }
    return best;
}

static void radar_draw(EidolonApp* app, Canvas* canvas) {
    RadarState* r = &app->radar;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 74, 10, "RADAR");
    canvas_set_font(canvas, FontSecondary);

    /* scope */
    canvas_draw_circle(canvas, RADAR_CX, RADAR_CY, RADAR_R);
    canvas_draw_circle(canvas, RADAR_CX, RADAR_CY, RADAR_R * 2 / 3);
    canvas_draw_circle(canvas, RADAR_CX, RADAR_CY, RADAR_R / 3);
    canvas_draw_line(canvas, RADAR_CX - RADAR_R, RADAR_CY, RADAR_CX + RADAR_R, RADAR_CY);
    canvas_draw_line(canvas, RADAR_CX, RADAR_CY - RADAR_R, RADAR_CX, RADAR_CY + RADAR_R);

    float brad = r->beam_deg * (float)M_PI / 180.0f;
    int bx = RADAR_CX + (int)((float)RADAR_R * sinf(brad));
    int by = RADAR_CY - (int)((float)RADAR_R * cosf(brad));
    canvas_draw_line(canvas, RADAR_CX, RADAR_CY, bx, by);

    uint8_t count = 0;
    for(uint8_t i = 0; i < RADAR_ECHOES; i++) {
        RadarEcho* e = &r->echoes[i];
        if(!e->active) continue;
        count++;
        float erad = e->angle * (float)M_PI / 180.0f;
        float rr = (float)RADAR_R * e->range / 100.0f;
        int ex = RADAR_CX + (int)(rr * sinf(erad));
        int ey = RADAR_CY - (int)(rr * cosf(erad));
        if(e->pinged) canvas_draw_disc(canvas, ex, ey, 2);
        else canvas_draw_circle(canvas, ex, ey, 2);
    }

    /* readouts */
    snprintf(buf, sizeof(buf), "ECHOES %u", count);
    canvas_draw_str(canvas, 74, 22, buf);
    RadarEcho* near_e = radar_nearest(app);
    if(near_e) {
        snprintf(buf, sizeof(buf), "BRG %03d", (int)near_e->angle);
        canvas_draw_str(canvas, 74, 32, buf);
        snprintf(buf, sizeof(buf), "RNG %d", (int)near_e->range);
        canvas_draw_str(canvas, 74, 41, buf);
        canvas_draw_str(
            canvas, 74, 50, near_e->range < 30 ? "CLOSE" : near_e->range < 65 ? "NEAR" : "FAR");
    } else {
        canvas_draw_str(canvas, 74, 32, "NO ECHO");
    }

    if(furi_get_tick() < r->answer_until) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 100, 62, AlignCenter, AlignBottom, r->answer);
        canvas_set_font(canvas, FontSecondary);
    } else {
        canvas_draw_str(canvas, 74, 62, "OK: YES/NO");
    }
}

static bool radar_input(EidolonApp* app, InputEvent* event) {
    RadarState* r = &app->radar;
    if(event->type == InputTypeRelease && event->key == InputKeyOk) {
        float act = eidolon_activity(app);
        float p_yes = 0.35f + act * 0.3f;
        bool yes = (float)(furi_hal_random_get() % 1000) < p_yes * 1000.0f;
        snprintf(r->answer, sizeof(r->answer), "%s", yes ? "YES" : "NO");
        r->answer_until = furi_get_tick() + 3500;
        eidolon_log_event(app, "RADAR", "question answered: %s (theater)", r->answer);
        eidolon_beep(app, yes ? 1300.0f : 700.0f, 0.3f, 90);
        return true;
    }
    return false;
}

const EidolonScreen eidolon_screen_radar = {
    .enter = radar_enter,
    .exit = radar_exit,
    .tick = radar_tick,
    .draw = radar_draw,
    .input = radar_input,
};
