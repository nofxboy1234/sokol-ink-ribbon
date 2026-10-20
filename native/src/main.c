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
#include "light.h"
#include "grace_state.h"
#include "level.h"
#include "pathfind.h"
#include "player.h"
#include "render.h"
#include "savegame.h"
#include "throw.h"
#include "webexport.h"
#include "levels.h"

#include "dbgui/dbgui.h"
#include "tooltip.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


// Grace's state (see grace_state_machine.png): every change goes through
// Paused. Paused is the only state where world time (enemy, doors, run
// clock) is frozen; the action menu above her shows only then.
typedef enum {
    GRACE_PAUSED = 0,
    GRACE_WALKING,
    GRACE_RUNNING,
    GRACE_WAITING,
    // just hurt: can't move or act for GRACE_HURT_SECONDS (time runs), then Waiting
    GRACE_HURT,
} grace_mode_t;

// The action being set up in the Paused menu, before Ok.
typedef enum {
    SETUP_NONE = 0,
    SETUP_WALK,
    SETUP_RUN,
    SETUP_WAIT,
    SETUP_THROW,
} grace_setup_t;

typedef enum {
    GRACE_ACT_WALK = 0,
    GRACE_ACT_RUN,
    GRACE_ACT_WAIT,
    GRACE_ACT_OK,
    GRACE_ACT_CANCEL,
    GRACE_ACT_PAUSE,
    GRACE_ACT_THROW,
} grace_action_t;

// Two map clicks this close in time and space pause an action.
#define DOUBLE_CLICK_SECONDS 0.33f
#define DOUBLE_CLICK_SLOP_PX 24.0f
// Touch has no right click: holding a finger on Grace this long opens Quick Equip.
// How long Grace flashes after a hit.
#define GRACE_FLASH_SECONDS 1.0f
// Then she breathes fast (stressed) until this long after the hit.
#define GRACE_STRESS_SECONDS 5.0f
// She is Hurt (can't move) this long, then waits.
#define GRACE_HURT_SECONDS 5.0f
// The enemy stands and waits this long after hurting her.
#define ENEMY_REST_SECONDS 8.0f
// The bottle's impact: the ring pulses out, then everything fades.
#define IMPACT_PULSE_SECONDS 0.25f
#define IMPACT_FADE_SECONDS 0.35f

static struct {
    sg_pass_action pass_action;
    grace_mode_t grace;
    grace_mode_t last;      // her last action (Walk/Run/Wait), shown while Paused
    grace_mode_t prev;      // the state before the current action (it returns there)
    grace_setup_t setup;
    bool menu_open;         // the action icons around Grace (Paused only)
    bool quick_open;        // the Quick Equip icons around Grace (Paused only)
    // A move interrupted by a pause: where she was headed and how, so the
    // cyan route stays drawn and a throw can resume it.
    bool pending;
    bool pending_run;
    bool resume_wait;       // paused mid-wait: a double click goes back to waiting
    int pending_x, pending_y;
    double grace_flash_until;
    double grace_stress_until;  // fast, stressed breathing after a hit
    double grace_hurt_until;    // Hurt ends (then Waiting)
    double impact_at;       // real clock of the last impact, < 0 for none
    int impact_x, impact_y;
    bool setup_has_target;  // Walk/Run: a reachable cell was picked
    bool aim_enemy;         // Throw: the aim is locked onto the enemy itself
    int setup_x, setup_y;
    path_t setup_route;     // the rope to the picked cell
    double clock;           // real seconds, for double-click timing
    double last_click_at;
    float last_click_x, last_click_y;
    level_t level;
    path_t preview;
    path_t route;
    // Touch has no hover, so the green preview only shows while a finger is down.
    bool touch_pointer;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
    int last_steps;
    bool last_moving;
    bool on_stairs;
    unsigned int revision;
    double elapsed;
    bool paused;
    bool save_menu;
    bool dead;
    int last_save_slot;
    bool section_revealed[LEVEL_MAX_SECTIONS];
    // Realtime input (Phase 0 spike: sokol delivers key events to the C
    // callback even with html5.bubble_key_events set -- bubbling only stops
    // preventDefault -- so WASD/E/Q/X are read here, gated by app.paused so
    // typing in React overlays never moves her).
    bool key_w, key_a, key_s, key_d;
    bool run_latch;          // E while walking: run until she stops
    float stick_x, stick_y;  // analogue stick vector from React, -1..1
    bool stick_was_live;     // (legacy field, unused by the twin-stick aim)
    bool aiming;             // throw aim mode: mouse/aim-stick direction + reticle
    float aim_wx, aim_wy;    // the reticle's world position (cells, float)
    float aim_stick_x, aim_stick_y;  // the right (aim) stick vector, -1..1
    bool aim_stick_live;     // aim stick pushed past the dead zone
    bool aim_from_stick;     // aim mode was entered from the aim stick
    float aim_dir_x, aim_dir_y;      // current aim direction (unit)
    float aim_len;           // current aim length in cells (<= THROW_RANGE_CELLS)
    bool touch_active;
    bool touch_moved;
    float touch_start_x, touch_start_y;
    int respawn_x, respawn_y;
    bool gl_full;      // last frame overflowed sokol_gl (tests)
    int gl_vertices;   // vertices it used (tests)
} app;

#define SGL_MAX_VERTICES (1 << 19)
#define SGL_MAX_COMMANDS (1 << 16)

#define TOUCH_SLOP_PX 14.0f

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

// Stepping onto a staircase moves Grace to the linked one. She has to step
// off before the stairs fire again, so arriving does not bounce her back.
static void take_stairs(void) {
    int px, py;
    player_cell(&px, &py);
    const obj_t* s = level_stairs_at(grid_level(), px, py);
    if (!s) {
        app.on_stairs = false;
        return;
    }
    if (app.on_stairs) {
        return;
    }
    app.on_stairs = true;
    const obj_t* dst = level_obj_by_id(grid_level(), s->link_id);
    if (!dst) {
        return;
    }
    player_set_cell(dst->x + dst->w / 2, dst->y + dst->h / 2);
    app.route.count = 0;
    camera_follow(player_x(), player_y());
    camera_recentre();
    reveal_sections();
    bump_revision();
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
        if ((app.touch_pointer && !app.touch_active) || !pathfind(px, py, cx, cy, &app.preview)) {
            app.preview.count = 0;
        }
    } else {
        app.hover_x = -1;
        app.hover_y = -1;
        app.preview.count = 0;
    }
}

static grace_state_t state_of(grace_mode_t m) {
    return m == GRACE_WALKING ? GS_WALK : m == GRACE_RUNNING ? GS_RUN : m == GRACE_HURT ? GS_HURT : GS_WAIT;
}

static grace_mode_t mode_of(grace_state_t s) {
    return s == GS_WALK ? GRACE_WALKING : s == GS_RUN ? GRACE_RUNNING : s == GS_HURT ? GRACE_HURT : GRACE_WAITING;
}

// May she go from the state she is in (or paused in) to this one?
static bool grace_can(grace_state_t to) {
    return grace_state_allowed(state_of(app.last), to);
}

static void set_grace(grace_mode_t mode) {
    if (app.grace != mode) {
        app.grace = mode;
        if (mode != GRACE_PAUSED) {
            if (mode != app.last) {
                app.prev = app.last;
            }
            app.last = mode;
        }
        app.menu_open = mode == GRACE_PAUSED;  // open whenever she pauses, closed during actions
        app.quick_open = false;
        bump_revision();
    }
}

static bool setup_is_move(void) {
    return app.setup == SETUP_WALK || app.setup == SETUP_RUN;
}

// Ok is enabled once every choice is made: Wait needs none, Walk/Run need
// a reachable target.
static bool setup_ready(void) {
    return app.setup == SETUP_WAIT || ((setup_is_move() || app.setup == SETUP_THROW) && app.setup_has_target);
}

// How she would throw right now: on the move if a paused walk or run is
// waiting to resume, otherwise standing still.
static throw_stance_t grace_stance(void) {
    if (app.pending) {
        return app.pending_run ? THROW_STANCE_RUN : THROW_STANCE_WALK;
    }
    return THROW_STANCE_WAIT;
}

static void clear_pending(void);

// A state that finished on its own: back to the one before it, paused.
// A walk or run carries on from the remembered route (pending) on resume;
// Wait resumes waiting.
static void grace_finish(grace_state_t finished, grace_state_t previous) {
    grace_state_t next = grace_state_finish(finished, previous, app.pending);
    if (next == GS_WAIT) {
        clear_pending();
        app.resume_wait = true;
    }
    if (app.grace != GRACE_PAUSED) {
        player_halt();
    }
    app.last = mode_of(next);
    app.prev = GRACE_WAITING;
    app.grace = GRACE_WAITING;  // so set_grace sees a change
    set_grace(GRACE_PAUSED);
}

static void clear_pending(void) {
    app.pending = false;
    app.resume_wait = false;
    app.route.count = 0;
}

// Carry on with the action she was paused in (a double click on the map):
// the remembered walk or run to its target, or waiting. False if there is none.
static bool grace_resume(void) {
    if (app.pending) {
        bool run = app.pending_run;
        player_move_to(app.pending_x, app.pending_y, run ? PLAYER_RUN : PLAYER_WALK);
        if (!player_is_moving()) {
            clear_pending();
            return false;
        }
        app.route = *player_path();
        app.pending = false;
        set_grace(run ? GRACE_RUNNING : GRACE_WALKING);
        return true;
    }
    if (app.resume_wait) {
        app.resume_wait = false;
        set_grace(GRACE_WAITING);
        return true;
    }
    return false;
}

// The pointer is over the enemy's drawn circle (where it actually is, which
// may be between cells), and it is in view.
#define AIM_ENEMY_RADIUS 0.64f  // cells: the enemy's drawn radius
static bool pointer_on_enemy(void) {
    if (app.ui_captured) {
        return false;
    }
    if (!grid_is_revealed((int)floorf(enemy_x()), (int)floorf(enemy_y()))) {
        return false;
    }
    float ex, ey;
    camera_cell_to_screen(enemy_x(), enemy_y(), &ex, &ey);
    float dx = app.mouse_x - ex, dy = app.mouse_y - ey;
    float r = AIM_ENEMY_RADIUS * camera_cell_px();
    return dx * dx + dy * dy <= r * r;
}

static void enemy_cell(int* x, int* y) {
    *x = (int)floorf(enemy_x());
    *y = (int)floorf(enemy_y());
}

// Lock the aim onto the enemy, if a throw can reach its cell.
static void pick_throw_enemy(void) {
    int px, py, ex, ey;
    player_cell(&px, &py);
    enemy_cell(&ex, &ey);
    if (!throw_in_reach(px, py, ex, ey)) {
        return;
    }
    app.setup_has_target = true;
    app.aim_enemy = true;
    app.setup_x = ex;
    app.setup_y = ey;
    bump_revision();
}

static void pick_throw(int x, int y) {
    int px, py;
    player_cell(&px, &py);
    if (!throw_in_reach(px, py, x, y)) {
        return;  // out of range, a wall in the way, or her own cell
    }
    app.setup_has_target = true;
    app.aim_enemy = false;
    app.setup_x = x;
    app.setup_y = y;
    bump_revision();
}

// Throw the bottle; she stays Paused.
static bool throw_confirm(void) {
    int px, py;
    player_cell(&px, &py);
    throw_stance_t stance = grace_stance();
    throw_result_t result;
    bool ok = app.aim_enemy ? throw_resolve_at_enemy(px, py, app.setup_x, app.setup_y, stance, &result)
                            : throw_resolve(px, py, app.setup_x, app.setup_y, stance, &result);
    if (!ok) {
        return false;
    }
    app.impact_at = app.clock;
    app.impact_x = app.setup_x;
    app.impact_y = app.setup_y;
    // The throw is done at once: back to what she was doing, still paused
    // (a paused walk keeps its route; a double click carries on).
    grace_finish(GS_THROW, state_of(app.last));
    bump_revision();
    return true;
}

static void clear_setup(void) {
    app.setup = SETUP_NONE;
    app.setup_has_target = false;
    app.aim_enemy = false;
    app.setup_route.count = 0;
    bump_revision();
}

static void pick_target(int x, int y) {
    int px, py;
    player_cell(&px, &py);
    if (!pathfind(px, py, x, y, &app.setup_route) || app.setup_route.count < 2) {
        return;  // a wall or unreachable cell is ignored
    }
    app.setup_has_target = true;
    app.setup_x = x;
    app.setup_y = y;
    bump_revision();
}

// A move order that crosses a table edge stops at the edge instead: onto
// the nearest floor cell next to the climbable, or (from the top) onto the
// nearest top cell beside a floor cell. The Climb prompt there does the rest.
static void adjust_move_target(int* tx, int* ty) {
    int px, py;
    player_cell(&px, &py);
    bool target_top = cell_on_climbable(*tx, *ty);
    bool she_top = player_on_top();
    if (target_top == she_top) {
        return;
    }
    const level_t* lv = grid_level();
    int best = 1 << 30, bx = px, by = py;
    path_t route;
    if (!she_top && target_top) {
        for (int i = 0; i < lv->obj_count; i++) {
            const obj_t* o = &lv->objs[i];
            if (o->kind != OBJ_OBSTACLE || !o->climbable || !(*tx >= o->x && *tx < o->x + (o->w > 0 ? o->w : 1) && *ty >= o->y && *ty < o->y + (o->h > 0 ? o->h : 1))) continue;
            for (int y = o->y - 1; y <= o->y + (o->h > 0 ? o->h : 1); y++) {
                for (int x = o->x - 1; x <= o->x + (o->w > 0 ? o->w : 1); x++) {
                    if (cell_on_climbable(x, y) || !grid_is_floor(x, y)) continue;
                    if (pathfind(px, py, x, y, &route) && route.count < best) { best = route.count; bx = x; by = y; }
                }
            }
        }
    } else {
        for (int i = 0; i < lv->obj_count; i++) {
            const obj_t* o = &lv->objs[i];
            if (o->kind != OBJ_OBSTACLE || !o->climbable) continue;
            for (int y = o->y; y < o->y + (o->h > 0 ? o->h : 1); y++) {
                for (int x = o->x; x < o->x + (o->w > 0 ? o->w : 1); x++) {
                    bool edge = false;
                    for (int dy = -1; dy <= 1 && !edge; dy++) for (int dx = -1; dx <= 1; dx++) edge |= !cell_on_climbable(x + dx, y + dy) && grid_is_floor(x + dx, y + dy);
                    if (!edge) continue;
                    int d = abs(x - *tx) + abs(y - *ty);
                    if (pathfind(px, py, x, y, &route) && route.count + d < best) { best = route.count + d; bx = x; by = y; }
                }
            }
        }
    }
    if (best < (1 << 30)) { *tx = bx; *ty = by; }
}

static bool grace_do(grace_action_t action) {
    if (app.grace == GRACE_HURT) {
        return false;  // she can't do anything while hurt
    }
    switch (action) {
        case GRACE_ACT_WALK:
        case GRACE_ACT_RUN:
        case GRACE_ACT_WAIT:
            // Picking an action locks the others until Ok or Cancel.
            if (app.grace != GRACE_PAUSED || app.setup != SETUP_NONE ||
                !grace_can(action == GRACE_ACT_WALK ? GS_WALK : action == GRACE_ACT_RUN ? GS_RUN : GS_WAIT)) {
                return false;
            }
            app.setup = action == GRACE_ACT_WALK ? SETUP_WALK
                      : action == GRACE_ACT_RUN  ? SETUP_RUN
                                                 : SETUP_WAIT;
            app.quick_open = false;
            bump_revision();
            return true;
        case GRACE_ACT_THROW:
            if (app.grace != GRACE_PAUSED || app.setup != SETUP_NONE || !throw_active() ||
                !grace_can(GS_THROW) || !throw_allowed(grace_stance())) {
                return false;
            }
            app.setup = SETUP_THROW;
            app.quick_open = false;
            bump_revision();
            return true;
        case GRACE_ACT_OK:
            if (app.grace != GRACE_PAUSED || !setup_ready()) {
                return false;
            }
            if (app.setup == SETUP_THROW) {
                if (!throw_confirm()) {
                    return false;
                }
            } else if (app.setup == SETUP_WAIT) {
                clear_pending();  // waiting gives up the old target
                set_grace(GRACE_WAITING);
            } else {
                bool run = app.setup == SETUP_RUN;
                adjust_move_target(&app.setup_x, &app.setup_y);
                player_move_to(app.setup_x, app.setup_y, run ? PLAYER_RUN : PLAYER_WALK);
                if (player_is_moving()) {
                    app.route = *player_path();
                    app.pending = false;
                    app.resume_wait = false;
                    set_grace(run ? GRACE_RUNNING : GRACE_WALKING);
                }
            }
            clear_setup();
            return true;
        case GRACE_ACT_CANCEL:
            if (app.setup == SETUP_NONE) {
                return false;
            }
            clear_setup();
            app.menu_open = true;  // back to choosing an action
            return true;
        case GRACE_ACT_PAUSE:
            if (app.grace == GRACE_PAUSED) {
                return false;
            }
            if ((app.grace == GRACE_WALKING || app.grace == GRACE_RUNNING) && app.route.count > 1) {
                // keep where she was headed: the route is redrawn from where she stops
                int last = app.route.count - 1;
                app.pending_x = app.route.x[last];
                app.pending_y = app.route.y[last];
                app.pending_run = app.grace == GRACE_RUNNING;
                app.pending = true;
            }
            app.resume_wait = app.grace == GRACE_WAITING;
            player_halt();  // stop at the nearest cell
            app.route.count = 0;
            if (app.pending) {
                int px, py;
                player_cell(&px, &py);
                if (!pathfind(px, py, app.pending_x, app.pending_y, &app.route) || app.route.count < 2) {
                    clear_pending();
                }
            }
            set_grace(GRACE_PAUSED);
            return true;
    }
    return false;
}

// The unified interact trigger (Space key on desktop, a tap on the right
// stick's centre on mobile): use whatever she faces, then her own cell, then the
// cells around her -- pickups, switches, doors, and the climb prompt alike.
static bool input_held(void) {
    return app.key_w || app.key_a || app.key_s || app.key_d ||
           app.stick_x * app.stick_x + app.stick_y * app.stick_y >= 0.12f * 0.12f;
}

static bool interact_trigger(void) {
    if (app.grace == GRACE_HURT || input_held() || player_climbing()) {
        return false;
    }
    player_stop();
    int px, py;
    player_cell(&px, &py);
    static const int facing_dx[4] = { 0, 1, 0, -1 };
    static const int facing_dy[4] = { -1, 0, 1, 0 };
    int f = player_facing();
    int cells[10][2];
    int n = 0;
    cells[n][0] = px + facing_dx[f]; cells[n][1] = py + facing_dy[f]; n++;
    cells[n][0] = px; cells[n][1] = py; n++;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            cells[n][0] = px + dx; cells[n][1] = py + dy; n++;
        }
    }
    for (int i = 0; i < n; i++) {
        if (doors_interact(cells[i][0], cells[i][1]) || interact_at(cells[i][0], cells[i][1])) {
            bump_revision();
            return true;
        }
    }
    return false;
}

// Aim mode (Q on desktop, the right aim stick on touch): any time the
// bottle is active, moving or not -- while aiming her speed is capped to
// a walk (see input_update), so a throw is standing (80% stance rules no
// longer roll: hitscan decides) or walking, never running.
static bool aim_enter(void) {
    if (app.aiming) {
        app.aiming = false;
        app.aim_from_stick = false;
        bump_revision();
        return true;  // Q again cancels
    }
    if (!throw_active() || player_climbing() || app.grace == GRACE_HURT || app.grace == GRACE_PAUSED) {
        return false;
    }
    app.aiming = true;
    app.aim_dir_x = 1.0f;
    app.aim_dir_y = 0.0f;
    app.aim_len = 3.0f;
    app.aim_wx = player_x() + app.aim_dir_x * app.aim_len;
    app.aim_wy = player_y();
    app.aim_enemy = false;
    bump_revision();
    return true;
}

static void aim_update(void) {
    if (!app.aiming) {
        return;
    }
    float stick_mag = sqrtf(app.aim_stick_x * app.aim_stick_x + app.aim_stick_y * app.aim_stick_y);
    if (stick_mag >= 0.12f) {
        // the aim stick: direction sets the line, push distance the length
        app.aim_dir_x = app.aim_stick_x / stick_mag;
        app.aim_dir_y = app.aim_stick_y / stick_mag;
        app.aim_len = (stick_mag > 1.0f ? 1.0f : stick_mag) * THROW_RANGE_CELLS;
    } else if (app.aim_stick_x == 0.0f && app.aim_stick_y == 0.0f && !app.touch_pointer) {
        float wx, wy;
        camera_screen_to_world(app.mouse_x, app.mouse_y, &wx, &wy);
        float dx = wx - player_x(), dy = wy - player_y();
        float len = sqrtf(dx * dx + dy * dy);
        if (len >= 0.05f) {
            app.aim_dir_x = dx / len;
            app.aim_dir_y = dy / len;
            app.aim_len = len > THROW_RANGE_CELLS ? THROW_RANGE_CELLS : len;
        }
    }
    app.aim_wx = player_x() + app.aim_dir_x * app.aim_len;
    app.aim_wy = player_y() + app.aim_dir_y * app.aim_len;
    app.aim_enemy = false;
    // snap onto the enemy when the reticle passes close to it: the aim
    // direction bends to its centre, and the hitscan confirms the line
    if (grid_is_revealed((int)floorf(enemy_x()), (int)floorf(enemy_y()))) {
        float ex = enemy_x() - app.aim_wx, ey = enemy_y() - app.aim_wy;
        if (ex * ex + ey * ey <= 0.75f * 0.75f) {
            float edx = enemy_x() - player_x(), edy = enemy_y() - player_y();
            float elen = sqrtf(edx * edx + edy * edy);
            if (elen > 0.05f) {
                app.aim_dir_x = edx / elen;
                app.aim_dir_y = edy / elen;
                app.aim_len = elen;
            }
            app.aim_wx = enemy_x();
            app.aim_wy = enemy_y();
            app.aim_enemy = true;
        }
    }
}

static bool aim_ok_now(void) {
    // a throw always lands somewhere (floor or blocker smash); only a
    // stub of an aim -- released inside the dead zone -- is refused
    return app.aiming && throw_active() && app.aim_len >= 0.5f;
}

// Throw along the aim ray: proper hitscan (see throw.c). The stance is
// whatever she is actually doing -- aiming caps her to a walk, so Run
// can never reach the resolve.
static bool aim_confirm(void) {
    if (!aim_ok_now()) {
        return false;
    }
    throw_stance_t stance = !player_is_moving() ? THROW_STANCE_WAIT
                          : player_move_mode() == PLAYER_RUN ? THROW_STANCE_RUN
                                                             : THROW_STANCE_WALK;
    throw_result_t result;
    float ix = 0.0f, iy = 0.0f;
    if (!throw_resolve_ray(player_x(), player_y(), app.aim_dir_x, app.aim_dir_y, app.aim_len,
                           stance, &result, &ix, &iy)) {
        return false;
    }
    app.impact_at = app.clock;
    app.impact_x = (int)floorf(ix);
    app.impact_y = (int)floorf(iy);
    app.aiming = false;
    app.aim_from_stick = false;
    app.aim_stick_live = false;
    app.aim_enemy = false;
    bump_revision();
    return true;
}

static const obj_t* hover_interactable(void);
static void update_hover(void);

// Grace's dot is not an input: clicking, right-clicking, tapping or
// long-pressing her does nothing. Quick Equip opens only via the Z key
// or a long hold on the right stick's centre (both go through
// quick_toggle); interact is X or a right-stick centre tap. Map clicks
// elsewhere still use doors, items and climbs.

// Quick Equip opens/closes only via the Z key or a long hold on the
// right stick's centre (web_quick_toggle). While she stands still only;
// moving or hurt: no equip.
static bool quick_toggle(void) {
    if (app.grace == GRACE_HURT || player_is_moving() || player_climbing()) {
        return false;
    }
    app.quick_open = !app.quick_open;
    bump_revision();
    return true;
}

static void handle_click(void) {
    if (app.grace == GRACE_HURT) {
        return;  // no moving, no acting while hurt
    }
    if (app.aiming) {
        aim_confirm();  // desktop: the click throws (a grey reticle refuses)
        return;
    }
    // Everything else a map click used to do is gone: Grace's dot takes
    // no input (see above), and neither do the X discovery/interact
    // prompts -- clicking or tapping the X marker, the item, a door or a
    // climb prompt cell does nothing. Interact is only the X key or a
    // right-stick centre tap (interact_trigger). A map click that is not
    // an aim-throw therefore does nothing at all.
}

// A hit worsens Grace's health one step. A hit while in Danger sends her back
// to the last save point (or the start) at full health.
static void grace_hurt(void) {
    int health = health_state();
    if (health >= HEALTH_DANGER) {
        app.dead = true;  // a hit in Danger kills her; the timer stops (frame skips the world)
    } else {
        health_set(health + 1);
    }
    app.grace_flash_until = app.clock + GRACE_FLASH_SECONDS;
    app.grace_stress_until = app.clock + GRACE_STRESS_SECONDS;
    // Hurt: she stops where she is, drops what she was doing and can't act.
    // A second hit while Hurt starts the 5 s again.
    player_halt();
    app.aiming = false;
    app.key_w = app.key_a = app.key_s = app.key_d = false;
    app.run_latch = false;
    clear_pending();
    if (app.setup != SETUP_NONE) {
        clear_setup();
    }
    app.grace_hurt_until = app.clock + GRACE_HURT_SECONDS;
    set_grace(GRACE_HURT);
    app.menu_open = false;
    bump_revision();
}

// The enemy caught her: she is hurt, and it stands and waits a while (8 s,
// longer than her 5 s Hurt, so she gets 3 s to get away).
static void enemy_hit(void) {
    grace_hurt();
    enemy_rest(ENEMY_REST_SECONDS);
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    // sokol_gl's default 64k vertices ran out once enough of the hospital was
    // revealed (walls, floor dots, items, routes all go through it); anything
    // drawn after that point -- Grace's dot is drawn late -- was silently
    // dropped. Room for the whole level revealed, with headroom.
    sgl_setup(&(sgl_desc_t){
        .max_vertices = SGL_MAX_VERTICES,
        .max_commands = SGL_MAX_COMMANDS,
        .logger.func = slog_func,
    });
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
    throw_init();
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
    app.ui_captured = false;
    app.last_steps = 0;
    app.last_moving = false;
    app.revision = 0;
    app.elapsed = 0.0;
    app.dead = false;
    app.last_save_slot = -1;
    app.paused = false;
    app.save_menu = false;
    app.grace = GRACE_WAITING;  // realtime: the world is live from the start
    app.last = GRACE_WAITING;
    app.prev = GRACE_WAITING;
    app.menu_open = false;  // the radial action menu is gone
    app.quick_open = false;
    app.pending = false;
    app.resume_wait = false;
    app.aiming = false;
    app.run_latch = false;
    app.impact_at = -1.0;
    app.grace_flash_until = -1.0;
    app.grace_stress_until = -1.0;
    app.grace_hurt_until = -1.0;
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
            o->kind != OBJ_FILE && o->kind != OBJ_SAFE && !(o->kind == OBJ_OBSTACLE && o->openable)) {
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
        if (o->kind == OBJ_ITEM && !light_item_visible(i)) {
            continue;
        }
        if ((o->kind == OBJ_OBSTACLE && o->openable) && o->open) {
            continue;
        }
        bool covered = false;
        if ((o->kind == OBJ_OBSTACLE && o->openable)) {
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

// Item labels: every item Grace could pick up right now (within the
// one-cell reach Space and the hover popup use) that she can see gets its
// name above it, white with a black border. Drawn on the foreground draw
// list only, so they never take input. Same visibility rules as the hover.
#define ITEM_LABEL_REACH 1
static int item_labels_shown;
static char item_label_last[64];

static bool item_label_visible(int i) {
    const obj_t* o = &app.level.objs[i];
    return o->kind == OBJ_ITEM && grid_is_revealed(o->x, o->y) && !items_taken(i) &&
           !openable_covers_closed(o->x, o->y) && light_item_visible(i) &&
           interact_distance(i) <= ITEM_LABEL_REACH;
}

static void draw_item_labels(void) {
    item_labels_shown = 0;
    if (app.paused || app.dead || level_complete()) {
        return;
    }
    for (int i = 0; i < app.level.obj_count; i++) {
        if (!item_label_visible(i)) {
            continue;
        }
        const obj_t* o = &app.level.objs[i];
        float sx, sy;
        camera_cell_to_screen((float)o->x + 0.5f, (float)o->y, &sx, &sy);
        const char* name = item_display_name(o->item_type);
        game_label(name, sx, sy - 6.0f);  // same pixel space as the hover popup
        snprintf(item_label_last, sizeof item_label_last, "%s", name);
        item_labels_shown++;
    }
}

// Extra dear-imgui UI drawn inside the dbgui frame (the hover tooltip).
void _dbgui_extra(void) {
    draw_item_labels();
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

// Realtime movement input: WASD keys and/or the analogue stick become a
// velocity on Grace. Releasing everything puts her in Waiting (the world
// stays live, D1) and clears the run latch. While aiming, movement input
// aims (stick) or is ignored (keys) -- she can only throw standing still.
static void input_update(void) {
    if (app.paused || app.dead || level_complete() || app.grace == GRACE_HURT || player_climbing()) {
        player_set_velocity(0.0f, 0.0f);
        return;
    }
    float ix = (app.key_d ? 1.0f : 0.0f) - (app.key_a ? 1.0f : 0.0f) + app.stick_x;
    float iy = (app.key_s ? 1.0f : 0.0f) - (app.key_w ? 1.0f : 0.0f) + app.stick_y;
    float mag = sqrtf(ix * ix + iy * iy);
    if (mag > 1.0f) {
        ix /= mag;
        iy /= mag;
        mag = 1.0f;
    }
    if (mag < 0.12f) {
        player_set_velocity(0.0f, 0.0f);
        app.run_latch = false;  // stopped: the next move walks again
        if (app.grace == GRACE_WALKING || app.grace == GRACE_RUNNING) {
            set_grace(GRACE_WAITING);
        }
        return;
    }
    bool keys = app.key_w || app.key_a || app.key_s || app.key_d;
    bool run = keys ? app.run_latch : mag > 0.5f;
    if (app.aiming) {
        run = false;  // aiming caps her to a walk, whatever the latch/push
    }
    float speed;
    if (keys) {
        speed = run ? PLAYER_RUN_SPEED_CELLS : PLAYER_WALK_SPEED_CELLS;
    } else if (mag <= 0.5f) {
        speed = PLAYER_WALK_SPEED_CELLS * (mag / 0.5f);  // proportional in the walk band
    } else {
        speed = PLAYER_WALK_SPEED_CELLS +
                (PLAYER_RUN_SPEED_CELLS - PLAYER_WALK_SPEED_CELLS) * ((mag - 0.5f) / 0.5f);
    }
    if (app.aiming && speed > PLAYER_WALK_SPEED_CELLS) {
        speed = PLAYER_WALK_SPEED_CELLS;  // noise follows: player mode derives from speed
    }
    player_set_velocity(ix / mag * speed, iy / mag * speed);
    grace_mode_t want = run ? GRACE_RUNNING : GRACE_WALKING;
    if (app.grace != want) {
        set_grace(want);
    }
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    app.clock += (double)dt;
    if (throw_sync()) {  // a used-up slot greys out; a found bottle fills it again
        bump_revision();
    }
    if (lighter_on() && !inventory_has(ITEM_LIGHTER)) {
        lighter_toggle();  // no lighter, no light
        bump_revision();
    }
    if (player_climbing()) {
        player_update(dt);  // the 0.35 s climb runs even while she is Paused
        camera_follow(player_x(), player_y());
        if (!player_climbing()) {
            bump_revision();
        }
    }
    if (!app.paused && app.grace == GRACE_HURT && app.clock >= app.grace_hurt_until) {
        set_grace(GRACE_WAITING);  // Hurt over: Waiting (world live)
    }
    input_update();
    aim_update();
    if (!app.paused && !app.dead && !level_complete() && app.grace != GRACE_PAUSED) {
        {
            // sight needs her lit: her Lighter, or a level light on her cell
            int px, py;
            player_cell(&px, &py);
            enemy_set_grace_lit((lighter_on() && inventory_has(ITEM_LIGHTER)) || light_cell_lit(px, py));
        }
        player_note_time((int)(app.elapsed * 1000.0));
        player_update(dt);
        if (interact_check_complete()) { bump_revision(); }
        float noise = !player_is_moving() ? 0.0f
            : player_move_mode() == PLAYER_RUN ? GRACE_NOISE_RUN : GRACE_NOISE_WALK;
        if (enemy_update(dt, player_x(), player_y(), noise)) {
            enemy_hit();
        }
        reveal_sections();
        take_stairs();

        if (doors_update(dt)) {
            bump_revision();
        }
        if (app.route.count > 1 && !player_is_moving()) {
            int px, py;
            player_cell(&px, &py);
            int last = app.route.count - 1;
            if (px == app.route.x[last] && py == app.route.y[last]) {
                app.route.count = 0;
            }
        }
        if ((app.grace == GRACE_WALKING || app.grace == GRACE_RUNNING) && !player_is_moving()) {
            clear_pending();
            app.run_latch = false;
            set_grace(GRACE_WAITING);  // arrived / stopped: Waiting, world live
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
    if (!app.paused) {
        camera_follow(player_x(), player_y());
        camera_update(dt);
    }
    light_update();
    update_hover();

    _dbgui_update();
    render_fx_t fx = { 0 };
    if (app.aiming) {
        fx.aim = true;
        fx.aim_wx = app.aim_wx;
        fx.aim_wy = app.aim_wy;
        fx.aim_ok = aim_ok_now();
        fx.aim_enemy = app.aim_enemy;
        fx.aim_ex = enemy_x();
        fx.aim_ey = enemy_y();
    }
    if (app.impact_at >= 0.0) {
        float age = (float)(app.clock - app.impact_at);
        if (age < IMPACT_PULSE_SECONDS + IMPACT_FADE_SECONDS) {
            fx.impact = true;
            fx.impact_x = app.impact_x;
            fx.impact_y = app.impact_y;
            fx.impact_pulse = age < IMPACT_PULSE_SECONDS ? age / IMPACT_PULSE_SECONDS : 1.0f;
            fx.impact_alpha = age < IMPACT_PULSE_SECONDS ? 1.0f
                            : 1.0f - (age - IMPACT_PULSE_SECONDS) / IMPACT_FADE_SECONDS;
        }
    }
    fx.grace_flash = app.clock < app.grace_flash_until ? (float)(app.grace_flash_until - app.clock) : 0.0f;
    fx.grace_stress = app.clock < app.grace_stress_until ? (float)(app.grace_stress_until - app.clock) : 0.0f;
    fx.grace_waiting = app.grace == GRACE_WAITING;
    fx.clock = (float)fmod(app.clock, 1000.0);
    render_set_fx(&fx);
    render_scene(-1, -1, NULL, &app.route);
    {
        sgl_error_t ge = sgl_error();
        app.gl_full = ge.vertices_full || ge.commands_full;
        app.gl_vertices = sgl_num_vertices();
    }

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

static void click_at(void) {
    light_update();
    update_hover();
    handle_click();
}

static void event(const sapp_event* e) {
    if (_dbgui_event_with_retval(e)) {
        return;
    }
    switch (e->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP: {
            bool down = e->type == SAPP_EVENTTYPE_KEY_DOWN;
            // React overlays freeze the world via app.paused; keys read then
            // (e.g. typing in Auth inputs) must never move or act for her.
            if (app.paused) {
                app.key_w = app.key_a = app.key_s = app.key_d = false;
                return;
            }
            switch (e->key_code) {
                case SAPP_KEYCODE_W: app.key_w = down; break;
                case SAPP_KEYCODE_A: app.key_a = down; break;
                case SAPP_KEYCODE_S: app.key_s = down; break;
                case SAPP_KEYCODE_D: app.key_d = down; break;
                case SAPP_KEYCODE_E:
                    // run latch: pressing E while walking (or while a move
                    // key is held, before the next frame lands) runs; while
                    // running it is a no-op (it never toggles run off)
                    if (down && !e->key_repeat &&
                        (app.grace == GRACE_WALKING || app.key_w || app.key_a || app.key_s || app.key_d)) {
                        app.run_latch = true;
                    }
                    break;
                case SAPP_KEYCODE_Q:
                    if (down && !e->key_repeat) {
                        aim_enter();
                    }
                    break;
                case SAPP_KEYCODE_SPACE:
                    // Space is the interact/confirm key (X is unbound now).
                    // While a React menu is open app.paused already guards
                    // this; the menu's own Space-accept never reaches here.
                    if (down && !e->key_repeat) {
                        interact_trigger();
                    }
                    break;
                case SAPP_KEYCODE_Z:
                    if (down && !e->key_repeat) {
                        quick_toggle();
                    }
                    break;
                default: break;
            }
            return;
        }
        case SAPP_EVENTTYPE_RESIZED:
            camera_set_viewport(sapp_width(), sapp_height());
            return;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            app.touch_pointer = false;
            app.mouse_x = e->mouse_x;
            app.mouse_y = e->mouse_y;
            return;
        case SAPP_EVENTTYPE_MOUSE_UP:
            app.mouse_x = e->mouse_x;
            app.mouse_y = e->mouse_y;
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                click_at();
            }
            // Right click does nothing: Grace's dot takes no input, and
            // Quick Equip is Z (desktop) or the right-stick hold (touch).
            return;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            app.touch_pointer = true;
            if (e->num_touches == 1) {
                app.touch_active = true;
                app.touch_moved = false;
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
                click_at();
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

TEST_EXPORT float player_px(void) {
    return player_x();
}

TEST_EXPORT float player_py(void) {
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

TEST_EXPORT int web_inventory_count(void) {
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

// Green Herb heals one step (Danger -> Caution -> Fine); a Mixed Herb (G+G)
// and a Med Injector heal fully.
WEB_EXPORT int web_inventory_use(int index) {
    int item = inventory_slot(index);
    int heal = item == ITEM_HERB ? 1 : (item == ITEM_MIXED_HERB || item == ITEM_INJECTOR) ? HEALTH_DANGER : 0;
    int health = health_state();
    if (heal == 0 || health == 0 || !inventory_clear_slot(index)) {
        return 0;
    }
    health_set(health - heal);
    bump_revision();
    return 1;
}

WEB_EXPORT int web_grace_mode(void) {
    return (int)app.grace;
}

WEB_EXPORT int web_grace_setup(void) {
    return (int)app.setup;
}

WEB_EXPORT int web_grace_menu_open(void) {
    return app.menu_open ? 1 : 0;
}

WEB_EXPORT int web_grace_last(void) {
    return (int)app.last;
}

WEB_EXPORT int web_grace_stance(void) {
    return (int)grace_stance();
}

TEST_EXPORT int web_grace_resumable(void) {
    return app.pending || app.resume_wait ? 1 : 0;
}

TEST_EXPORT int web_grace_pending(void) {
    return app.pending ? 1 : 0;
}

TEST_EXPORT int web_route_length(void) {
    return app.route.count;
}

WEB_EXPORT int web_quick_open(void) {
    return app.quick_open ? 1 : 0;
}

// Toggle Quick Equip from React (a long hold on the right stick's
// centre); the Z key and right-click on Grace route through the same
// quick_toggle, with the same standing-still conditions.
WEB_EXPORT int web_quick_toggle(void) {
    return quick_toggle() ? 1 : 0;
}

// Quick Equip slot 0..3 = N, E, S, W. Only East holds anything (the bottle):
// picking it toggles it as the active weapon and returns to the action menu.
WEB_EXPORT int web_quick_select(int slot) {
    if (app.quick_open && slot == 0 && inventory_has(ITEM_LIGHTER)) {
        // the Lighter (North) toggles in place; Quick Equip stays open
        lighter_toggle();
        bump_revision();
        return 1;
    }
    if (!app.quick_open || slot != 1 || !throw_equipped() || throw_empty()) {
        return 0;
    }
    throw_set_active(!throw_active());
    app.quick_open = false;
    bump_revision();
    return 1;
}

WEB_EXPORT int web_bottle_equipped(void) {
    return throw_equipped() ? 1 : 0;
}

WEB_EXPORT int web_light_at(int x, int y) {
    return light_cell_lit(x, y) ? 1 : 0;
}

WEB_EXPORT int web_enemy_angry(void) {
    return enemy_angry() > 0.0f ? 1 : 0;
}

TEST_EXPORT int web_aim_enemy(void) {
    return app.aim_enemy ? 1 : 0;
}

TEST_EXPORT int web_grace_hurt_ms(void) {
    return app.grace == GRACE_HURT && app.clock < app.grace_hurt_until
        ? (int)((app.grace_hurt_until - app.clock) * 1000.0) : 0;
}

// An item at this cell is on the map (lit now, or shown by the Lighter before).
TEST_EXPORT int web_item_visible_at(int x, int y) {
    for (int i = 0; i < app.level.obj_count; i++) {
        const obj_t* o = &app.level.objs[i];
        if (o->kind == OBJ_ITEM && o->x == x && o->y == y && !items_taken(i)) {
            return light_item_visible(i) ? 1 : 0;
        }
    }
    return 0;
}

TEST_EXPORT int web_grace_stress_ms(void) {
    return app.clock < app.grace_stress_until ? (int)((app.grace_stress_until - app.clock) * 1000.0) : 0;
}

// Tests: make the obstacle covering this cell climbable.
TEST_EXPORT int web_test_set_climbable(int x, int y) {
    for (int i = 0; i < app.level.obj_count; i++) {
        obj_t* o = &app.level.objs[i];
        if (o->kind == OBJ_OBSTACLE && x >= o->x && x < o->x + (o->w > 0 ? o->w : 1) && y >= o->y && y < o->y + (o->h > 0 ? o->h : 1)) {
            o->climbable = 1;
            bump_revision();
            return 1;
        }
    }
    return 0;
}

// Tests: give, equip and ready a bottle.
TEST_EXPORT int web_test_bottle_ready(void) {
    inventory_add(ITEM_BOTTLE);
    throw_set_equipped(true);
    throw_set_active(true);
    bump_revision();
    return throw_active() ? 1 : 0;
}

// Tests: throw at a cell from a standstill (the realtime aim's resolve).
TEST_EXPORT int web_test_throw_at(int x, int y) {
    if (!throw_active() || player_is_moving()) {
        return 0;
    }
    int px, py;
    player_cell(&px, &py);
    throw_result_t result;
    if (!throw_resolve(px, py, x, y, THROW_STANCE_WAIT, &result)) {
        return 0;
    }
    app.impact_at = app.clock;
    app.impact_x = x;
    app.impact_y = y;
    bump_revision();
    return 1;
}

// Tests: use a switch (or other interactable) at a cell.
TEST_EXPORT int web_test_interact(int x, int y) {
    bool used = interact_at(x, y);
    bump_revision();
    return used ? 1 : 0;
}

// Tests: make the enemy angry (as if hit) without a throw.
TEST_EXPORT void web_test_anger(void) {
    enemy_damage(0.05f);
}

WEB_EXPORT int web_bottle_empty(void) {
    return throw_empty() ? 1 : 0;
}

WEB_EXPORT int web_bottle_active(void) {
    return throw_active() ? 1 : 0;
}

WEB_EXPORT int web_bottle_equip(int on) {
    if (!throw_set_equipped(on != 0)) {
        return 0;
    }
    bump_revision();
    return 1;
}

WEB_EXPORT int web_throw_hit_chance(void) {
    // hitscan: a ray that reaches the enemy hits -- no percentage any more
    return 100;
}

TEST_EXPORT int web_throw_in_reach(int x, int y) {
    int px, py;
    player_cell(&px, &py);
    return throw_in_reach(px, py, x, y) ? 1 : 0;
}

TEST_EXPORT int web_cell_floor(int x, int y) {
    return grid_is_floor(x, y) && grid_is_revealed(x, y) ? 1 : 0;
}

WEB_EXPORT int web_throw_allowed(void) {
    // twin-stick: she can throw on the move too (aiming caps her to a walk)
    return throw_active() && app.grace != GRACE_HURT ? 1 : 0;
}

TEST_EXPORT float web_enemy_flash(void) {
    return enemy_flash();
}

TEST_EXPORT float web_grace_flash(void) {
    return app.clock < app.grace_flash_until ? (float)(app.grace_flash_until - app.clock) : 0.0f;
}

TEST_EXPORT int web_enemy_focus_x(void) {
    return enemy_focus_x();
}

TEST_EXPORT int web_enemy_focus_y(void) {
    return enemy_focus_y();
}

// Milliseconds since the last bottle impact, or -1.
TEST_EXPORT double web_impact_age_ms(void) {
    return app.impact_at < 0.0 ? -1.0 : (app.clock - app.impact_at) * 1000.0;
}

// Test hooks: seed the throw and enemy dice, hand Grace an item, put the
// enemy somewhere, and hit Grace.
TEST_EXPORT void web_test_seed(unsigned int seed) {
    throw_seed(seed);
    enemy_seed(seed * 2654435761u + 1u);
}

// Tests: locate level content by type/role instead of hardcoded cells.
// Packed result: (x << 16) | (y << 8) | extra, or -1 when absent.
// For items, dark=0 finds lit (visible) ones, dark=1 finds unlit ones;
// nth is the 0-based index among the matches.
TEST_EXPORT int web_test_find_item(int item, int nth, int dark) {
    int seen = 0;
    for (int i = 0; i < app.level.obj_count; i++) {
        obj_t* o = &app.level.objs[i];
        if (o->kind != OBJ_ITEM || o->item_type != item || items_taken(i)) {
            continue;
        }
        if (dark ? light_item_visible(i) : !light_item_visible(i)) {
            continue;
        }
        if (seen++ < nth) {
            continue;
        }
        return (o->x << 16) | (o->y << 8);
    }
    return -1;
}

// Tests: the nth object of a kind (obj_kind_t). extra = its width.
TEST_EXPORT int web_test_find_object(int kind, int nth) {
    int seen = 0;
    for (int i = 0; i < app.level.obj_count; i++) {
        obj_t* o = &app.level.objs[i];
        if ((int)o->kind != kind) {
            continue;
        }
        if (seen++ < nth) {
            continue;
        }
        return (o->x << 16) | (o->y << 8) | (o->w > 0 ? o->w : 1);
    }
    return -1;
}

// Tests: a door by role. 0 = first plain unlocked door, 1 = the
// key-linked (cherub) door, 2 = the first "gate", 3 = the "security
// door". extra = 1 when the door is horizontal.
TEST_EXPORT int web_test_find_door(int mode) {
    for (int i = 0; i < app.level.obj_count; i++) {
        obj_t* o = &app.level.objs[i];
        if (o->kind != OBJ_DOOR) {
            continue;
        }
        bool ok = false;
        if (mode == 0) {
            ok = o->state == DOOR_UNLOCKED && o->link_id == 0 && strcmp(o->name, "door") == 0;
        } else if (mode == 1) {
            ok = o->link_id > 0;
        } else if (mode == 2) {
            ok = strcmp(o->name, "gate") == 0;
        } else if (mode == 3) {
            ok = strcmp(o->name, "security door") == 0;
        }
        if (ok) {
            return (o->x << 16) | (o->y << 8) | (o->horizontal ? 1 : 0);
        }
    }
    return -1;
}

TEST_EXPORT int web_test_give(int item) {
    if (item < 0 || item >= ITEM_COUNT || !inventory_add(item)) {
        return 0;
    }
    bump_revision();
    return 1;
}

TEST_EXPORT void web_test_enemy_at(int x, int y) {
    enemy_place((float)x + 0.5f, (float)y + 0.5f);
}

// Tests: put the enemy anywhere, between cells too (hundredths of a cell).
TEST_EXPORT void web_test_enemy_place(int x100, int y100) {
    enemy_place((float)x100 / 100.0f, (float)y100 / 100.0f);
}

TEST_EXPORT float web_enemy_screen_x(void) {
    float sx, sy;
    camera_cell_to_screen(enemy_x(), enemy_y(), &sx, &sy);
    return sx / sapp_dpi_scale();
}

TEST_EXPORT float web_enemy_screen_y(void) {
    float sx, sy;
    camera_cell_to_screen(enemy_x(), enemy_y(), &sx, &sy);
    return sy / sapp_dpi_scale();
}

TEST_EXPORT void web_test_grace_at(int x, int y) {
    // (body below teleports her; also run the goal check a walk would run)
    player_set_cell(x, y);
    app.route.count = 0;
    app.pending = false;
    camera_follow(player_x(), player_y());
    camera_recentre();
    reveal_sections();
    interact_check_complete();
    bump_revision();
}

// Tests: did the last frame overflow sokol_gl's buffers (anything drawn
// after that point, Grace included, is silently dropped)? And how many
// vertices it used.
TEST_EXPORT int web_test_gl_full(void) {
    return app.gl_full ? 1 : 0;
}

TEST_EXPORT int web_test_gl_vertices(void) {
    return app.gl_vertices;
}

// Tests: the nth door, packed x<<16 | y<<8 | span<<1 | horizontal; -1 past the end.
TEST_EXPORT int web_test_door(int nth) {
    int seen = 0;
    for (int i = 0; i < app.level.obj_count; i++) {
        obj_t* o = &app.level.objs[i];
        if (o->kind != OBJ_DOOR) {
            continue;
        }
        if (seen++ == nth) {
            int span = o->span > 0 ? o->span : 1;
            return (o->x << 16) | (o->y << 8) | (span << 1) | (o->horizontal ? 1 : 0);
        }
    }
    return -1;
}

// Tests: how many item labels the last frame drew, and the last one's text.
TEST_EXPORT int web_test_item_labels(void) {
    return item_labels_shown;
}

TEST_EXPORT const char* web_test_item_label(void) {
    return item_label_last;
}

TEST_EXPORT int web_test_ui_wants_mouse(void) {
    return game_ui_wants_mouse() ? 1 : 0;
}

TEST_EXPORT void web_test_hurt(void) {
    grace_hurt();
}

WEB_EXPORT int web_grace_ready(void) {
    return setup_ready() ? 1 : 0;
}

// Move stick vector from React (-1..1); (0,0) is release. Movement only:
// aiming lives on the second stick (web_aim_input).
WEB_EXPORT void web_stick_input(float x, float y) {
    app.stick_x = x;
    app.stick_y = y;
}

// Aim stick vector from React (-1..1). Pushing it enters aim mode; its
// direction and push distance set the throw; releasing it throws -- or
// cancels if the release never left the dead zone.
WEB_EXPORT void web_aim_input(float x, float y) {
    float mag = sqrtf(x * x + y * y);
    if (mag >= 0.12f) {
        if (!app.aiming) {
            aim_enter();
            if (app.aiming) {
                app.aim_from_stick = true;
            }
        }
        if (app.aiming) {
            app.aim_stick_live = true;
            app.aim_stick_x = x;
            app.aim_stick_y = y;
        }
        return;
    }
    app.aim_stick_x = x;
    app.aim_stick_y = y;
    if (x == 0.0f && y == 0.0f) {
        if (app.aim_stick_live && app.aiming) {
            aim_confirm();  // release throws (a grey stub aim refuses inside)
        } else if (app.aim_from_stick && app.aiming) {
            app.aiming = false;  // dead-zone release cancels
            bump_revision();
        }
        app.aim_stick_live = false;
        app.aim_from_stick = false;
    } else {
        app.aim_stick_live = false;  // back inside the dead zone
    }
}

WEB_EXPORT int web_aim_toggle(void) {
    return aim_enter() ? 1 : 0;
}

WEB_EXPORT int web_aiming(void) {
    return app.aiming ? 1 : 0;
}

WEB_EXPORT int web_interact_trigger(void) {
    return interact_trigger() ? 1 : 0;
}

TEST_EXPORT int web_test_run_latch(void) {
    return app.run_latch ? 1 : 0;
}

// Hitscan probe for tests: ray from Grace along (dx,dy)/100 for len/100
// cells. 1 = enemy, 2 = blocker, 0 = open floor.
TEST_EXPORT int web_test_hitscan(int dx100, int dy100, int len100) {
    throw_scan_t scan;
    if (!throw_hitscan(player_x(), player_y(), (float)dx100 / 100.0f, (float)dy100 / 100.0f,
                       (float)len100 / 100.0f, &scan)) {
        return -1;
    }
    return scan.hit_enemy ? 1 : scan.blocked ? 2 : 0;
}

TEST_EXPORT int web_test_aiming(void) {
    return app.aiming ? 1 : 0;
}

TEST_EXPORT float web_test_aim_x(void) {
    return app.aim_wx;
}

TEST_EXPORT float web_test_aim_y(void) {
    return app.aim_wy;
}

WEB_EXPORT int web_grace_action(int action) {
    return grace_do((grace_action_t)action) ? 1 : 0;
}

// Grace's dot on screen, in CSS pixels from the canvas top-left.
WEB_EXPORT float web_grace_screen_x(void) {
    float sx, sy;
    camera_cell_to_screen(player_x(), player_y(), &sx, &sy);
    return sx / sapp_dpi_scale();
}

WEB_EXPORT float web_grace_screen_y(void) {
    float sx, sy;
    camera_cell_to_screen(player_x(), player_y(), &sx, &sy);
    return sy / sapp_dpi_scale();
}

// Test hook: move the camera off Grace (CSS pixels) to put her near an edge.
TEST_EXPORT void web_camera_pan(float dx, float dy) {
    camera_pan_pixels(dx * sapp_dpi_scale(), dy * sapp_dpi_scale());
}

WEB_EXPORT float web_cell_css_px(void) {
    return camera_cell_px() / sapp_dpi_scale();
}

TEST_EXPORT int web_enemy_state(void) {
    return (int)enemy_state();
}

TEST_EXPORT int web_doors_broken(void) {
    int count = 0;
    for (int i = 0; i < app.level.obj_count; i++) {
        const obj_t* o = &app.level.objs[i];
        count += o->kind == OBJ_DOOR && o->state == DOOR_BROKEN;
    }
    return count;
}

TEST_EXPORT int web_lights_broken(void) {
    return light_broken_count();
}

TEST_EXPORT int web_enemy_sense(void) {
    return (int)enemy_sense();
}

TEST_EXPORT float web_enemy_px(void) {
    return enemy_x();
}

TEST_EXPORT float web_enemy_py(void) {
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

// Tests: 0 brings back dark items (only lit or Lighter-shown ones show).
TEST_EXPORT void web_test_items_always_visible(int on) {
    light_set_items_always_visible(on != 0);
    bump_revision();
}

TEST_EXPORT void lighter_set(int on) {
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
WEB_EXPORT int web_level_complete(void) { return level_complete() ? 1 : 0; }
WEB_EXPORT int web_grace_dead(void) { return app.dead ? 1 : 0; }
WEB_EXPORT int web_last_save_slot(void) { return app.last_save_slot; }
WEB_EXPORT int web_inventory_capacity(void) { return inventory_capacity(); }
WEB_EXPORT int web_crafting_known(int index) { return crafting_known(index) ? 1 : 0; }
WEB_EXPORT int web_on_top(void) { return player_on_top() ? 1 : 0; }
// Keypad / cabinet bridges (React calls these from touch-friendly UI).
WEB_EXPORT int web_pending_safe(void) { return interact_pending_safe(); }
WEB_EXPORT int web_try_safe_code(int code) { return interact_try_safe_code(code) ? 1 : 0; }
WEB_EXPORT int web_pending_code_door(void) { return doors_pending_code(); }
WEB_EXPORT int web_try_door_code(int code) { return doors_try_code(code) ? 1 : 0; }

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
WEB_EXPORT int replay_t(int index) {
    return player_replay_t(index);
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
    throw_init();
    enemy_init(app.level.start_x, app.level.start_y);
    app.respawn_x = app.level.start_x;
    app.respawn_y = app.level.start_y;
    camera_set_follow(true);
    camera_follow(player_x(), player_y());
    camera_recentre();
    memset(app.section_revealed, 0, sizeof(app.section_revealed));
    reveal_sections();
    app.elapsed = 0.0;
    app.dead = false;
    app.last_save_slot = -1;
    app.paused = false;
    app.grace = GRACE_PAUSED;
    app.last = GRACE_WAITING;
    app.prev = GRACE_WAITING;
    app.menu_open = true;
    app.quick_open = false;
    app.pending = false;
    app.resume_wait = true;  // Paused Wait: a double click waits
    app.impact_at = -1.0;
    app.grace_flash_until = -1.0;
    app.grace_stress_until = -1.0;
    app.grace_hurt_until = -1.0;
    app.setup = SETUP_NONE;
    app.setup_has_target = false;
    app.aim_enemy = false;
    app.route.count = 0;
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
    app.last_save_slot = slot;
    player_cell(&app.respawn_x, &app.respawn_y);
    bump_revision();
    return 1;
}

WEB_EXPORT int load_game(int slot) {
    double e = 0.0;
    if (savegame_load(slot, &e)) {
        app.dead = false;
        app.last_save_slot = slot;
        app.elapsed = e / 1000.0;
        player_cell(&app.respawn_x, &app.respawn_y);
        if (!savegame_has_enemy(slot)) {
            enemy_init(app.respawn_x, app.respawn_y);  // an old slot: respawn it away from her
        }
        player_stop();
        app.grace = GRACE_PAUSED;
        app.last = GRACE_WAITING;
        app.prev = GRACE_WAITING;
        app.menu_open = true;
        app.quick_open = false;
        app.pending = false;
        app.resume_wait = true;  // Paused Wait: a double click waits
        app.setup = SETUP_NONE;
        app.setup_has_target = false;
        app.aim_enemy = false;
        app.route.count = 0;
        app.preview.count = 0;
        throw_sync();
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
