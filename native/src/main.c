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
#include "inventory.h"
#include "item.h"
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

static bool rect_contains(vec2_t min, vec2_t max, vec2_t p) {
    return (p.x >= min.x) && (p.x <= max.x) && (p.y >= min.y) && (p.y <= max.y);
}

static void handle_click(vec2_t screen_pos, player_move_t mode) {
    // the "Pick up" popup takes priority over map movement
    vec2_t pmin, pmax;
    if (render_popup_rect(&pmin, &pmax) && rect_contains(pmin, pmax, screen_pos)) {
        item_take(item_any_in_reach());
        return;
    }
    int cx, cy;
    camera_cell_at(screen_pos, &cx, &cy);
    player_move_to(cx, cy, mode);
}

// left-click walks, right-click runs
static player_move_t move_mode_for(sapp_mousebutton button) {
    return (button == SAPP_MOUSEBUTTON_RIGHT) ? PLAYER_MOVE_RUN : PLAYER_MOVE_WALK;
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
    items_init();
    inventory_init();

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
    render_draw_overlay();
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
        return;
    }
    if (e->type != SAPP_EVENTTYPE_MOUSE_DOWN) {
        return;
    }
    app.mouse_x = e->mouse_x;
    app.mouse_y = e->mouse_y;
    app.ui_captured = captured;
    if (captured) {
        return;
    }
    if (e->mouse_button != SAPP_MOUSEBUTTON_LEFT && e->mouse_button != SAPP_MOUSEBUTTON_RIGHT) {
        return;
    }
    handle_click((vec2_t){ e->mouse_x, e->mouse_y }, move_mode_for(e->mouse_button));
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
