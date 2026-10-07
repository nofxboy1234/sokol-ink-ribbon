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
#include "enemy.h"
#include "grid.h"
#include "inventory.h"
#include "item.h"
#include "player.h"
#include "render.h"

#include <stdbool.h>
#include <math.h>

#define PLAYER_START_X 9
#define PLAYER_START_Y 2

// A press that moves less than this counts as a click (move Grace) rather than
// a drag (pan the map).
#define DRAG_THRESHOLD 6.0f

static struct {
    sg_pass_action pass_action;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
    // drag-to-pan state
    bool dragging;
    bool dragged;
    player_move_t drag_mode;
    float press_x, press_y;
    // pinch-to-zoom state
    bool pinching;
    float pinch_dist;
    // enemies act on these, not on wall-clock time: the map is turn-based
    int last_player_step;
    // set when an attack landed, so the camera can snap back to Grace
    bool player_was_hit;
    // Grace's cell as of the last enemy turn
    int player_x, player_y;
} app;

static const float PINCH_MIN_DIST = 12.0f;

// Enemies take one step for every cell Grace covers, so they never move while
// Grace is standing still. Comparing against the player's own step counter keeps
// this to exactly one enemy turn per cell, however the frame rate varies.
static void update_enemies(void) {
    const int steps = player_steps_taken();
    if (steps == app.last_player_step) {
        return;
    }
    // one enemy turn per cell Grace covers
    for (int i = app.last_player_step; i < steps; i++) {
        enemy_turn();
    }
    app.last_player_step = steps;

    // an attack that landed respawns Grace somewhere else
    int cx, cy;
    player_cell(&cx, &cy);
    if ((cx != app.player_x) || (cy != app.player_y)) {
        app.player_was_hit = true;
        app.last_player_step = player_steps_taken();
    }
    app.player_x = cx;
    app.player_y = cy;
}

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
    enemy_init();

    app.hover_x = -1;
    app.hover_y = -1;
    app.ui_captured = false;
    app.dragging = false;
    app.dragged = false;
    app.drag_mode = PLAYER_MOVE_WALK;
    app.pinching = false;
    app.pinch_dist = 0.0f;
    app.last_player_step = 0;
    app.player_was_hit = false;
    app.player_x = PLAYER_START_X;
    app.player_y = PLAYER_START_Y;
    app.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            // lifted off pure black to the reference frame's shadow navy, so
            // walls read as lit surfaces in the dark rather than strokes on a
            // flat void
            .clear_value = { 0.027f, 0.063f, 0.149f, 1.0f },
        },
    };
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    player_update(dt);
    // enemies slide smoothly, but only on the frames where Grace actually moved
    enemy_update(dt);
    update_enemies();
    if (app.player_was_hit) {
        app.player_was_hit = false;
        // re-centre on her new position but keep the zoom, so a respawn does
        // not lurch the view back to the default scale
        camera_recentre();
    }
    camera_update();
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

static void begin_drag(const sapp_event* e) {
    app.dragging = true;
    app.dragged = false;
    app.press_x = e->mouse_x;
    app.press_y = e->mouse_y;
    app.drag_mode = move_mode_for(e->mouse_button);
}

// Dragging pans the map; a press that never moved past the threshold is a
// click, so Grace still moves to the cell that was pressed.
static void update_drag(const sapp_event* e) {
    if (!app.dragging) {
        return;
    }
    const float dx = e->mouse_x - app.press_x;
    const float dy = e->mouse_y - app.press_y;
    if (!app.dragged && ((fabsf(dx) > DRAG_THRESHOLD) || (fabsf(dy) > DRAG_THRESHOLD))) {
        app.dragged = true;
    }
    if (app.dragged) {
        camera_pan(dx, dy);
        app.press_x = e->mouse_x;
        app.press_y = e->mouse_y;
    }
}

static void end_drag(void) {
    const bool was_click = app.dragging && !app.dragged;
    app.dragging = false;
    app.dragged = false;
    if (was_click) {
        handle_click((vec2_t){ app.mouse_x, app.mouse_y }, app.drag_mode);
    }
}

// Touch handling for phones: one finger drags the map, two fingers pinch to zoom
// about the point between them, and a tap (which never moved) is a click so
// Grace still moves to the cell that was tapped.
static float touch_distance(const sapp_touchpoint* a, const sapp_touchpoint* b) {
    const float dx = b->pos_x - a->pos_x;
    const float dy = b->pos_y - a->pos_y;
    return sqrtf(dx * dx + dy * dy);
}

static void handle_touches(const sapp_event* e) {
    if (e->num_touches >= 2) {
        const sapp_touchpoint* a = &e->touches[0];
        const sapp_touchpoint* b = &e->touches[1];
        const float dist = touch_distance(a, b);
        const vec2_t mid = { (a->pos_x + b->pos_x) * 0.5f, (a->pos_y + b->pos_y) * 0.5f };
        if (app.pinching && (app.pinch_dist > PINCH_MIN_DIST)) {
            camera_zoom_at(mid, dist / app.pinch_dist);
        }
        app.pinching = true;
        app.pinch_dist = dist;
        // a pinch is not a tap, and it cancels any drag in progress
        app.dragging = false;
        app.dragged = false;
        return;
    }
    if (e->num_touches == 1) {
        if ((e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) && !app.pinching) {
            app.dragging = true;
            app.dragged = false;
            app.press_x = e->touches[0].pos_x;
            app.press_y = e->touches[0].pos_y;
            app.drag_mode = PLAYER_MOVE_WALK;
        } else if (app.dragging) {
            // reuse the drag path, which reads the mouse position off the event
            sapp_event fake = *e;
            fake.mouse_x = e->touches[0].pos_x;
            fake.mouse_y = e->touches[0].pos_y;
            update_drag(&fake);
        }
        return;
    }
    // last finger lifted
    if (app.pinching) {
        app.pinching = false;
        app.pinch_dist = 0.0f;
        return;
    }
    if (app.dragging) {
        const bool was_click = !app.dragged;
        app.dragging = false;
        app.dragged = false;
        if (was_click && e->num_touches == 0) {
            // a tap with no movement is a click
            app.mouse_x = app.press_x;
            app.mouse_y = app.press_y;
            handle_click((vec2_t){ app.mouse_x, app.mouse_y }, app.drag_mode);
        }
    }
}

static void event(const sapp_event* e) {
    const bool captured = _dbgui_event_with_retval(e);
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        app.mouse_x = e->mouse_x;
        app.mouse_y = e->mouse_y;
        app.ui_captured = captured;
        update_drag(e);
        return;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        if (!captured) {
            // vertical wheel zooms about the cursor; horizontal still pans
            if (e->scroll_y != 0.0f) {
                camera_zoom_step((vec2_t){ e->mouse_x, e->mouse_y }, e->scroll_y);
            }
            if (e->scroll_x != 0.0f) {
                camera_pan_cells(-0.3333f * e->scroll_x, 0.0f);
            }
        }
        return;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_ENTER || e->type == SAPP_EVENTTYPE_MOUSE_LEAVE) {
        return;
    }
    if ((e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) || (e->type == SAPP_EVENTTYPE_TOUCHES_MOVED) ||
        (e->type == SAPP_EVENTTYPE_TOUCHES_ENDED) || (e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED)) {
        handle_touches(e);
        return;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_UP) {
        app.mouse_x = e->mouse_x;
        app.mouse_y = e->mouse_y;
        app.ui_captured = captured;
        if (!captured) {
            end_drag();
        } else {
            app.dragging = false;
            app.dragged = false;
        }
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
    if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
        // middle click stops Grace where she is
        player_stop();
        camera_set_follow(true);
        return;
    }
    if (e->mouse_button != SAPP_MOUSEBUTTON_LEFT && e->mouse_button != SAPP_MOUSEBUTTON_RIGHT) {
        return;
    }
    begin_drag(e);
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
