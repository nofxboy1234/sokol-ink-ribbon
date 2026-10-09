//------------------------------------------------------------------------------
//  main.c
//  Grace map runtime: grid map, smooth follow camera, A* movement, section
//  reveal and the sokol debug menus. Rendering lives in render.c.
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"

#include "camera.h"
#include "crafting.h"
#include "doors.h"
#include "grid.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "pathfind.h"
#include "player.h"
#include "render.h"
#include "webexport.h"
#include "levels.h"

#include <math.h>
#include <string.h>


static struct {
    sg_pass_action pass_action;
    level_t level;
    path_t preview;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
    int last_steps;
    bool last_moving;
    unsigned int revision;
    double elapsed;
    bool section_revealed[LEVEL_MAX_SECTIONS];
} app;

static void bump_revision(void);

// Reveal whole map sections as Grace steps into them (large chunks).
static void reveal_sections(void) {
    int px, py;
    player_cell(&px, &py);
    const level_t* lv = grid_level();
    for (int i = 0; i < lv->section_count && i < LEVEL_MAX_SECTIONS; i++) {
        if (app.section_revealed[i]) {
            continue;
        }
        const section_t* s = &lv->sections[i];
        if (px >= s->x && px < s->x + s->w && py >= s->y && py < s->y + s->h) {
            app.section_revealed[i] = true;
            grid_reveal_rect(s->x, s->y, s->w, s->h);
            bump_revision();
        }
    }
}

static void bump_revision(void) {
    app.revision++;
}

static void update_hover(void) {
    if (app.ui_captured) {
        app.hover_x = -1;
        app.hover_y = -1;
        app.preview.count = 0;
        return;
    }
    int cx, cy;
    camera_screen_to_cell(app.mouse_x, app.mouse_y, &cx, &cy);
    if (grid_is_floor(cx, cy)) {
        app.hover_x = cx;
        app.hover_y = cy;
        int px, py;
        player_cell(&px, &py);
        if (!pathfind(px, py, cx, cy, &app.preview)) {
            app.preview.count = 0;
        }
    } else {
        app.hover_x = -1;
        app.hover_y = -1;
        app.preview.count = 0;
    }
}

static void handle_click(player_move_t mode) {
    if (app.hover_x < 0 || app.hover_y < 0) {
        return;
    }
    if (doors_interact(app.hover_x, app.hover_y)) {
        bump_revision();
        return;
    }
    if (interact_at(app.hover_x, app.hover_y)) {
        bump_revision();
        return;
    }
    player_move_to(app.hover_x, app.hover_y, mode);
    bump_revision();
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    render_init();

    if (!level_from_json(&app.level, embed_level_01_json)) {
        level_init(&app.level);
    }
    grid_init(&app.level);
    player_init(app.level.start_x, app.level.start_y);
    inventory_init();
    items_init();
    doors_init();
    interact_init();
    health_init();
    camera_init();
    camera_set_viewport(sapp_width(), sapp_height());
    camera_follow(player_x(), player_y());
    camera_recentre();
    reveal_sections();

    app.hover_x = -1;
    app.hover_y = -1;
    app.preview.count = 0;
    app.ui_captured = false;
    app.last_steps = 0;
    app.last_moving = false;
    app.revision = 0;
    app.elapsed = 0.0;
    app.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 1.0f, 1.0f, 1.0f, 1.0f },
        },
    };
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    player_update(dt);
    camera_follow(player_x(), player_y());
    camera_update(dt);
    reveal_sections();
    update_hover();

    if (items_update()) {
        bump_revision();
    }
    if (doors_update(dt)) {
        bump_revision();
    }
    app.elapsed += (double)dt;

    int steps = player_steps_taken();
    bool moving = player_is_moving();
    if (steps != app.last_steps || moving != app.last_moving) {
        app.last_steps = steps;
        app.last_moving = moving;
        bump_revision();
    }

    render_scene(app.hover_x, app.hover_y, &app.preview);

    sg_begin_pass(&(sg_pass){
        .action = app.pass_action,
        .swapchain = sglue_swapchain(),
    });
    sgl_draw();
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    render_shutdown();
    sgl_shutdown();
    sg_shutdown();
}

static void click_at(sapp_mousebutton button) {
    update_hover();
    handle_click(button == SAPP_MOUSEBUTTON_RIGHT ? PLAYER_RUN : PLAYER_WALK);
}

static void event(const sapp_event* e) {
    switch (e->type) {
        case SAPP_EVENTTYPE_RESIZED:
            camera_set_viewport(sapp_width(), sapp_height());
            return;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            app.mouse_x = e->mouse_x;
            app.mouse_y = e->mouse_y;
            return;
        case SAPP_EVENTTYPE_MOUSE_UP:
            app.mouse_x = e->mouse_x;
            app.mouse_y = e->mouse_y;
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT || e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
                click_at(e->mouse_button);
            }
            return;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
            if (e->num_touches >= 1) {
                app.mouse_x = e->touches[0].pos_x;
                app.mouse_y = e->touches[0].pos_y;
            }
            return;
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
            if (e->num_touches == 0) {
                click_at(SAPP_MOUSEBUTTON_LEFT);
            }
            return;
        default:
            return;
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

WEB_EXPORT unsigned int ui_revision(void) {
    return app.revision;
}

WEB_EXPORT int grid_width(void) {
    return grid_cols();
}

WEB_EXPORT int grid_height(void) {
    return grid_rows();
}

WEB_EXPORT int player_cell_x(void) {
    int x, y;
    player_cell(&x, &y);
    return x;
}

WEB_EXPORT int player_cell_y(void) {
    int x, y;
    player_cell(&x, &y);
    return y;
}

WEB_EXPORT int player_is_walking(void) {
    return player_is_moving() ? 1 : 0;
}

WEB_EXPORT float player_px(void) {
    return player_x();
}

WEB_EXPORT float player_py(void) {
    return player_y();
}

WEB_EXPORT int player_total_steps(void) {
    return player_steps_taken();
}

WEB_EXPORT int level_revealed(void) {
    return grid_revealed_count();
}

WEB_EXPORT int level_cell_px(void) {
    return (int)camera_cell_px();
}

WEB_EXPORT int web_inventory_count(void) {
    return inventory_count();
}

WEB_EXPORT int web_inventory_slot(int index) {
    return inventory_slot(index);
}

WEB_EXPORT int web_inventory_has(int item_type) {
    return inventory_has(item_type) ? 1 : 0;
}

WEB_EXPORT int health_value(void) {
    return health_state();
}

WEB_EXPORT int items_collected(void) {
    return items_taken_count();
}

WEB_EXPORT int doors_discovered(void) {
    return doors_discovered_count();
}

WEB_EXPORT int lighter_value(void) {
    return lighter_on() ? 1 : 0;
}

WEB_EXPORT void lighter_set(int on) {
    if ((on != 0) != lighter_on()) {
        lighter_toggle();
        bump_revision();
    }
}

WEB_EXPORT int web_files_found(void) {
    return files_found();
}

WEB_EXPORT int file_code(int index) {
    return file_code_at(index);
}

WEB_EXPORT const char* file_name(int index) {
    return file_name_at(index);
}

WEB_EXPORT int web_saves_made(void) {
    return saves_made();
}

WEB_EXPORT int craft_recipe(int index) {
    if (craft(index)) {
        bump_revision();
        return 1;
    }
    return 0;
}

WEB_EXPORT int replay_length(void) {
    return player_replay_length();
}

WEB_EXPORT int replay_x(int index) {
    return player_replay_x(index);
}

WEB_EXPORT int replay_y(int index) {
    return player_replay_y(index);
}

WEB_EXPORT void game_reset(void) {
    if (!level_from_json(&app.level, embed_level_01_json)) {
        level_init(&app.level);
    }
    grid_init(&app.level);
    player_init(app.level.start_x, app.level.start_y);
    inventory_init();
    items_init();
    doors_init();
    interact_init();
    health_init();
    camera_set_follow(true);
    camera_follow(player_x(), player_y());
    camera_recentre();
    memset(app.section_revealed, 0, sizeof(app.section_revealed));
    reveal_sections();
    app.elapsed = 0.0;
    app.last_steps = 0;
    app.last_moving = false;
    bump_revision();
}

WEB_EXPORT double run_elapsed_ms(void) {
    return app.elapsed * 1000.0;
}
