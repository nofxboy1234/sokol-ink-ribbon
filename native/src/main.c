//------------------------------------------------------------------------------
//  main.c
//  Grid map app: glowing walls, a player dot and A* movement, with the
//  sokol debug menus. Rendering lives in render.c.
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_gl.h"
#include "dbgui/dbgui.h"

#include "camera.h"
#include "grid.h"
#include "player.h"
#include "render.h"

#include <stdbool.h>

#define PLAYER_START_X 9
#define PLAYER_START_Y 2

static struct {
    sg_pass_action pass_action;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
} app;

static void update_hover(void) {
    if (app.ui_captured) {
        app.hover_x = -1;
        app.hover_y = -1;
        return;
    }
    int cx, cy;
    camera_cell_at((vec2_t){ app.mouse_x, app.mouse_y }, &cx, &cy);
    if (grid_is_floor(cx, cy)) {
        app.hover_x = cx;
        app.hover_y = cy;
    } else {
        app.hover_x = -1;
        app.hover_y = -1;
    }
}

static void handle_click(float screen_x, float screen_y) {
    int cx, cy;
    camera_cell_at((vec2_t){ screen_x, screen_y }, &cx, &cy);
    player_move_to(cx, cy);
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    render_init();
    _dbgui_setup();
    grid_init();
    player_init(PLAYER_START_X, PLAYER_START_Y);

    app.hover_x = -1;
    app.hover_y = -1;
    app.ui_captured = false;
    app.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f },
        },
    };
}

static void frame(void) {
    camera_update();
    player_update((float)sapp_frame_duration());
    update_hover();

    render_scene(app.hover_x, app.hover_y);

    _dbgui_update();
    sg_begin_pass(&(sg_pass){
        .action = app.pass_action,
        .swapchain = sglue_swapchain(),
    });
    sgl_draw();
    _dbgui_draw();
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    _dbgui_shutdown();
    render_shutdown();
    sg_shutdown();
}

static void event(const sapp_event* e) {
    const bool captured = _dbgui_event_with_retval(e);
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        app.mouse_x = e->mouse_x;
        app.mouse_y = e->mouse_y;
        app.ui_captured = captured;
    } else if (e->type == SAPP_EVENTTYPE_MOUSE_DOWN && e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        app.mouse_x = e->mouse_x;
        app.mouse_y = e->mouse_y;
        app.ui_captured = captured;
        if (!captured) {
            handle_click(e->mouse_x, e->mouse_y);
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .event_cb = event,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "sokol-ink-ribbon",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
