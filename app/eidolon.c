#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>

typedef struct {
    FuriMessageQueue* event_queue;
    ViewPort* view_port;
    Gui* gui;
} EidolonApp;

static void eidolon_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignBottom, "EIDOLON");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignBottom, "ghost hunting kit");
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, "press Back to exit");
}

static void eidolon_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    EidolonApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

int32_t eidolon_main(void* p) {
    UNUSED(p);
    EidolonApp* app = malloc(sizeof(EidolonApp));
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, eidolon_draw_callback, app);
    view_port_input_callback_set(app->view_port, eidolon_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeRelease && event.key == InputKeyBack) {
                running = false;
            }
        }
    }

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    free(app);
    return 0;
}
