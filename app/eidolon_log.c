#include "eidolon.h"

void eidolon_fmt_1dp(char* buf, size_t size, float value) {
    int32_t scaled = (int32_t)(value * 10.0f + (value >= 0 ? 0.5f : -0.5f));
    int32_t whole = scaled / 10;
    int32_t frac = scaled % 10;
    if(frac < 0) frac = -frac;
    snprintf(buf, size, "%ld.%ld", (long)whole, (long)frac);
}

bool eidolon_log_start(EidolonApp* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    app->start_tick = furi_get_tick();
    snprintf(
        app->session_name,
        sizeof(app->session_name),
        "session-%04d%02d%02d-%02d%02d%02d.log",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);

    storage_simply_mkdir(app->storage, STORAGE_APP_DATA_PATH_PREFIX);
    app->log_file = storage_file_alloc(app->storage);
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_APP_DATA_PATH_PREFIX, app->session_name);
    if(!storage_file_open(app->log_file, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        app->log_open = false;
        return false;
    }
    app->log_open = true;
    eidolon_log_event(app, "SESSION", "eidolon session start");
    return true;
}

void eidolon_log_stop(EidolonApp* app) {
    if(!app->log_open) return;
    eidolon_log_event(app, "SESSION", "eidolon session end");
    storage_file_close(app->log_file);
    storage_file_free(app->log_file);
    app->log_file = NULL;
    app->log_open = false;
}

void eidolon_log_event(EidolonApp* app, const char* tag, const char* fmt, ...) {
    char msg[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* keep a short copy for the menu footer */
    snprintf(app->last_event, sizeof(app->last_event), "%.8s: %.52s", tag, msg);

    if(!app->log_open || !app->log_file) return;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint32_t elapsed = (furi_get_tick() - app->start_tick) / 1000;
    char line[224];
    int len = snprintf(
        line,
        sizeof(line),
        "%04d-%02d-%02d %02d:%02d:%02d [+%lus] [%s] %s\n",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second,
        (unsigned long)elapsed,
        tag,
        msg);
    if(len > 0) {
        storage_file_write(app->log_file, line, (size_t)len);
        storage_file_sync(app->log_file);
    }
}
