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
#include "enemy.h"
#include "grid.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "pathfind.h"
#include "player.h"
#include "render.h"
#include "savegame.h"
#include "webexport.h"
#include "levels.h"

#include "dbgui/dbgui.h"
#include "tooltip.h"

#include <math.h>
#include <string.h>


static struct {
    sg_pass_action pass_action;
    level_t level;
    path_t preview;
    path_t route;
    bool targeting;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
    int last_steps;
    bool last_moving;
    unsigned int revision;
    double elapsed;
    bool paused;
    bool save_menu;
    bool section_revealed[LEVEL_MAX_SECTIONS];
    bool touch_active;
    bool touch_moved;
    float touch_start_x, touch_start_y;
    float touch_hold;
    int respawn_x, respawn_y;
} app;

// A long press on the map runs, like a right click.
#define TOUCH_RUN_HOLD 0.45f
#define TOUCH_SLOP_PX 14.0f

#define ENEMY_STUN_SECONDS 3.0f
#define HEALTH_DANGER 2
// How far Grace's footsteps carry, in cells; standing still is silent.
#define GRACE_NOISE_WALK 4.0f
#define GRACE_NOISE_RUN 8.0f

static void bump_revision(void);

// Reveal whole map sections as Grace steps into them (large chunks). Sections
// that overlap the one she entered are part of the same chunk, so reveal them
// together.
static bool sections_overlap(const section_t* a, const section_t* b) {
    return a->x < b->x + b->w && b->x < a->x + a->w &&
           a->y < b->y + b->h && b->y < a->y + a->h;
}

static void reveal_sections(void) {
    int px, py;
    player_cell(&px, &py);
    const level_t* lv = grid_level();
    for (int i = 0; i < lv->section_count && i < LEVEL_MAX_SECTIONS; i++) {
        const section_t* s = &lv->sections[i];
        if (px >= s->x && px < s->x + s->w && py >= s->y && py < s->y + s->h) {
            if (!app.section_revealed[i]) {
                app.section_revealed[i] = true;
                grid_reveal_rect(s->x, s->y, s->w, s->h);
                bump_revision();
            }
            for (int j = 0; j < lv->section_count; j++) {
                if (!app.section_revealed[j] && sections_overlap(s, &lv->sections[j])) {
                    app.section_revealed[j] = true;
                    const section_t* o = &lv->sections[j];
                    grid_reveal_rect(o->x, o->y, o->w, o->h);
                    bump_revision();
                }
            }
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
        if (!app.targeting || !pathfind(px, py, cx, cy, &app.preview)) {
            app.preview.count = 0;
        }
    } else {
        app.hover_x = -1;
        app.hover_y = -1;
        app.preview.count = 0;
    }
}

static void handle_click(player_move_t mode) {
    // A click while Grace walks steers her straight away; it also brings
    // the green preview back.
    app.targeting = true;
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
    if (player_is_moving()) {
        app.route = *player_path();
        app.targeting = false;
        app.preview.count = 0;
    } else {
        app.route.count = 0;
    }
    bump_revision();
}

// A hit worsens Grace's health one step. A hit while in Danger sends her back
// to the last save point (or the start) at full health.
static void enemy_hit(void) {
    int health = health_state();
    if (health >= HEALTH_DANGER) {
        player_set_cell(app.respawn_x, app.respawn_y);
        health_set(0);
        app.route.count = 0;
        app.targeting = true;
        app.preview.count = 0;
        camera_follow(player_x(), player_y());
        camera_recentre();
    } else {
        health_set(health + 1);
    }
    enemy_stun(ENEMY_STUN_SECONDS);
    bump_revision();
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    render_init();
    _dbgui_setup();

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
    savegame_init();
    enemy_init(app.level.start_x, app.level.start_y);
    app.respawn_x = app.level.start_x;
    app.respawn_y = app.level.start_y;
    camera_init();
    camera_set_viewport(sapp_width(), sapp_height());
    camera_follow(player_x(), player_y());
    camera_recentre();
    reveal_sections();

    app.hover_x = -1;
    app.hover_y = -1;
    app.preview.count = 0;
    app.route.count = 0;
    app.targeting = true;
    app.ui_captured = false;
    app.last_steps = 0;
    app.last_moving = false;
    app.revision = 0;
    app.elapsed = 0.0;
    app.paused = false;
    app.save_menu = false;
    app.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 1.0f, 1.0f, 1.0f, 1.0f },
        },
    };
}

// The interactable whose marker is under the mouse, used for the hover name
// popup. Doors accept a click out to two cells, matching the X popup.
static const obj_t* hover_interactable(void) {
    if (app.hover_x < 0 || app.hover_y < 0) {
        return NULL;
    }
    int px, py;
    player_cell(&px, &py);
    const level_t* lv = &app.level;
    int hx = app.hover_x;
    int hy = app.hover_y;
    for (int i = lv->obj_count - 1; i >= 0; i--) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_ITEM && o->kind != OBJ_DOOR && o->kind != OBJ_SWITCH &&
            o->kind != OBJ_TYPEWRITER && o->kind != OBJ_FUSEBOX &&
            o->kind != OBJ_FILE && o->kind != OBJ_SAFE && o->kind != OBJ_OPENABLE) {
            continue;
        }
        if (!grid_is_revealed(o->x, o->y)) {
            continue;
        }
        if (o->kind == OBJ_DOOR && o->state == DOOR_BROKEN) {
            continue;
        }
        if (o->kind == OBJ_ITEM && items_taken(i)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && openable_covers_closed(o->x, o->y)) {
            continue;
        }
        if (o->kind == OBJ_OPENABLE && o->open) {
            continue;
        }
        bool covered = false;
        if (o->kind == OBJ_OPENABLE) {
            int w = o->w > 0 ? o->w : 1;
            int h = o->h > 0 ? o->h : 1;
            covered = hx >= o->x && hx < o->x + w && hy >= o->y && hy < o->y + h;
        } else if (o->kind == OBJ_DOOR) {
            int span = o->span > 0 ? o->span : 1;
            covered = o->horizontal
                ? (hy >= o->y - 1 && hy <= o->y && hx >= o->x && hx < o->x + span)
                : (hx >= o->x - 1 && hx <= o->x && hy >= o->y && hy < o->y + span);
        } else {
            covered = (o->x == hx && o->y == hy);
        }
        if (!covered) {
            continue;
        }
        int reach = o->kind == OBJ_DOOR ? 2 : 1;
        return interact_distance(i) <= reach ? o : NULL;
    }
    return NULL;
}

// Extra dear-imgui UI drawn inside the dbgui frame (the hover tooltip).
void _dbgui_extra(void) {
    const obj_t* hl = hover_interactable();
    if (hl) {
        const char* name = hl->name[0] ? hl->name : obj_kind_name(hl->kind);
        if (hl->kind == OBJ_ITEM) {
            name = item_display_name(hl->item_type);
        } else if (hl->kind == OBJ_DOOR) {
            if (hl->state == DOOR_UNLOCKED) {
                name = hl->open ? "close" : "open";
            } else if (hl->state == DOOR_LOCKED) {
                name = "unlock";
            } else {
                name = "open";
            }
        }
        bool alert = (hl->kind == OBJ_ITEM && inventory_count() >= INV_SLOTS) ||
                     (hl->kind == OBJ_TYPEWRITER && !inventory_has(ITEM_INK_RIBBON));
        game_tooltip(name, app.mouse_x + 14.0f, app.mouse_y + 14.0f, alert);
    }
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    if (app.touch_active) {
        app.touch_hold += dt;
    }
    if (!app.paused) {
        player_update(dt);
        float noise = !player_is_moving() ? 0.0f
            : player_move_mode() == PLAYER_RUN ? GRACE_NOISE_RUN : GRACE_NOISE_WALK;
        if (enemy_update(dt, player_x(), player_y(), noise)) {
            enemy_hit();
        }
        camera_follow(player_x(), player_y());
        camera_update(dt);
        reveal_sections();

        if (doors_update(dt)) {
            bump_revision();
        }
        if (app.route.count > 1 && !player_is_moving()) {
            int px, py;
            player_cell(&px, &py);
            int last = app.route.count - 1;
            if (px == app.route.x[last] && py == app.route.y[last]) {
                app.route.count = 0;
                app.targeting = true;
            }
        }
        app.elapsed += (double)dt;

        int steps = player_steps_taken();
        bool moving = player_is_moving();
        if (steps != app.last_steps || moving != app.last_moving) {
            app.last_steps = steps;
            app.last_moving = moving;
            bump_revision();
        }
    }
    update_hover();

    _dbgui_update();
    render_scene(app.targeting ? app.hover_x : -1,
                 app.targeting ? app.hover_y : -1,
                 app.targeting ? &app.preview : NULL,
                 &app.route);

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
    sgl_shutdown();
    sg_shutdown();
}

static void click_at(sapp_mousebutton button) {
    update_hover();
    handle_click(button == SAPP_MOUSEBUTTON_RIGHT ? PLAYER_RUN : PLAYER_WALK);
}

static void event(const sapp_event* e) {
    if (_dbgui_event_with_retval(e)) {
        return;
    }
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
            if (e->num_touches == 1) {
                app.touch_active = true;
                app.touch_moved = false;
                app.touch_hold = 0.0f;
                app.touch_start_x = app.mouse_x = e->touches[0].pos_x;
                app.touch_start_y = app.mouse_y = e->touches[0].pos_y;
            } else {
                app.touch_active = false;
            }
            return;
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
            if (app.touch_active && e->num_touches >= 1) {
                app.mouse_x = e->touches[0].pos_x;
                app.mouse_y = e->touches[0].pos_y;
                float dx = app.mouse_x - app.touch_start_x;
                float dy = app.mouse_y - app.touch_start_y;
                if (dx * dx + dy * dy > TOUCH_SLOP_PX * TOUCH_SLOP_PX) {
                    app.touch_moved = true;
                }
            }
            return;
        case SAPP_EVENTTYPE_TOUCHES_ENDED: {
            // The lifted finger is still listed, flagged as changed.
            int held = 0;
            for (int i = 0; i < e->num_touches; i++) {
                if (!e->touches[i].changed) {
                    held++;
                }
            }
            if (held == 0 && app.touch_active && !app.touch_moved) {
                click_at(app.touch_hold >= TOUCH_RUN_HOLD ? SAPP_MOUSEBUTTON_RIGHT : SAPP_MOUSEBUTTON_LEFT);
            }
            if (held == 0) {
                app.touch_active = false;
            }
            return;
        }
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            app.touch_active = false;
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
        // let the browser handle keys so the HTML auth inputs can be typed into
        .html5.bubble_key_events = true,
        .html5.bubble_char_events = true,
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

WEB_EXPORT int web_inventory_move(int from, int to) {
    if (!inventory_move(from, to)) {
        return 0;
    }
    bump_revision();
    return 1;
}

WEB_EXPORT int web_inventory_clear(int index) {
    if (!inventory_clear_slot(index)) {
        return 0;
    }
    bump_revision();
    return 1;
}

WEB_EXPORT int health_value(void) {
    return health_state();
}

// Green Herb heals one step, Mixed Herb (G+G) heals fully.
WEB_EXPORT int web_inventory_use(int index) {
    int item = inventory_slot(index);
    int heal = item == ITEM_HERB ? 1 : item == ITEM_MIXED_HERB ? HEALTH_DANGER : 0;
    int health = health_state();
    if (heal == 0 || health == 0 || !inventory_clear_slot(index)) {
        return 0;
    }
    health_set(health - heal);
    bump_revision();
    return 1;
}

WEB_EXPORT int web_enemy_state(void) {
    return (int)enemy_state();
}

WEB_EXPORT int web_doors_broken(void) {
    int count = 0;
    for (int i = 0; i < app.level.obj_count; i++) {
        const obj_t* o = &app.level.objs[i];
        count += o->kind == OBJ_DOOR && o->state == DOOR_BROKEN;
    }
    return count;
}

WEB_EXPORT int web_enemy_sense(void) {
    return (int)enemy_sense();
}

WEB_EXPORT float web_enemy_px(void) {
    return enemy_x();
}

WEB_EXPORT float web_enemy_py(void) {
    return enemy_y();
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

WEB_EXPORT int web_goal_met(void) {
    return goal_met() ? 1 : 0;
}

WEB_EXPORT int web_crafting_count(void) {
    return crafting_count();
}

WEB_EXPORT int web_crafting_part_item(int index, int part) {
    return crafting_part_item(index, part);
}

WEB_EXPORT int web_crafting_part_qty(int index, int part) {
    return crafting_part_qty(index, part);
}

WEB_EXPORT int web_crafting_ready(int index) {
    return crafting_ready(index) ? 1 : 0;
}

WEB_EXPORT int web_item_count(void) {
    return ITEM_COUNT;
}

WEB_EXPORT const char* web_item_label(int item) {
    return item_display_name(item);
}

WEB_EXPORT const char* web_item_color(int item) {
    return item_color_hex(item);
}

WEB_EXPORT int web_item_icon(int item) {
    return item_icon_shape(item);
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
    enemy_init(app.level.start_x, app.level.start_y);
    app.respawn_x = app.level.start_x;
    app.respawn_y = app.level.start_y;
    camera_set_follow(true);
    camera_follow(player_x(), player_y());
    camera_recentre();
    memset(app.section_revealed, 0, sizeof(app.section_revealed));
    reveal_sections();
    app.elapsed = 0.0;
    app.paused = false;
    app.route.count = 0;
    app.targeting = true;
    app.preview.count = 0;
    app.last_steps = 0;
    app.last_moving = false;
    bump_revision();
}

WEB_EXPORT double run_elapsed_ms(void) {
    return app.elapsed * 1000.0;
}

WEB_EXPORT void set_paused(int paused) {
    app.paused = paused != 0;
}

WEB_EXPORT int take_save_menu(void) {
    return interact_take_save_menu() ? 1 : 0;
}

WEB_EXPORT int save_game(int slot) {
    if (slot < 0 || slot >= SAVE_SLOTS || !inventory_remove(ITEM_INK_RIBBON)) {
        return 0;
    }
    savegame_save(slot, app.elapsed * 1000.0);
    player_cell(&app.respawn_x, &app.respawn_y);
    bump_revision();
    return 1;
}

WEB_EXPORT int load_game(int slot) {
    double e = 0.0;
    if (savegame_load(slot, &e)) {
        app.elapsed = e / 1000.0;
        player_cell(&app.respawn_x, &app.respawn_y);
        enemy_init(app.respawn_x, app.respawn_y);
        app.route.count = 0;
        app.targeting = true;
        app.preview.count = 0;
        camera_follow(player_x(), player_y());
        bump_revision();
        return 1;
    }
    return 0;
}

WEB_EXPORT int save_used(int slot) {
    return savegame_used(slot) ? 1 : 0;
}

WEB_EXPORT double save_time(int slot) {
    return savegame_time(slot);
}
