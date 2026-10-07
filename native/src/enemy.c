#include "enemy.h"
#include "grid.h"
#include "inventory.h"
#include "item.h"
#include "pathfind.h"
#include "player.h"
#include <math.h>
#include <stdlib.h>

// Grace is noticed within this many cells and attacked from an adjacent one.
#define NOTICE_CELLS 3
#define ATTACK_CELLS 1

// Chance Grace slips an attack. Tuned so a fight lasts a few exchanges rather
// than ending immediately or dragging on forever.
#define DODGE_CHANCE 0.55f

// Enemies start away from Grace so the player gets a moment before being seen.
static const int ENEMY_START_X[ENEMY_COUNT] = { 11 };
static const int ENEMY_START_Y[ENEMY_COUNT] = { 4 };

// How far an enemy will wander from where it is when it picks a patrol
// destination, so it does not cross the whole map every time it thinks.
#define PATROL_RANGE 6

// Enemy movement speeds, matching Grace's own. A patrolling enemy walks; one
// that has noticed her runs, so a chase reads as different from a stroll.
#define ENEMY_WALK_SPEED (CELL * 2.0f)
#define ENEMY_RUN_SPEED (CELL * 4.0f)

typedef struct {
    vec2_t pos;
    int cell_x, cell_y;
    // the cell it is walking to, and the step of the path it is on
    int target_x, target_y;
    path_t path;
    int path_idx;
    bool moving;
    enemy_mode_t mode;
    // how long the current plan lasts, in Grace-steps
    int plan_steps;
} enemy_t;

static enemy_t enemies[ENEMY_COUNT];

static int rand_range(int lo, int hi) {
    // inclusive lo, exclusive hi
    if (hi <= lo) {
        return lo;
    }
    return lo + (rand() % (hi - lo));
}

static bool in_bounds(int x, int y) {
    return (x >= 0) && (x < GRID_W) && (y >= 0) && (y < GRID_H) && grid_is_floor(x, y);
}

static int manhattan(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}

// Pick a floor cell to walk to. When chasing, that is Grace's cell. When
// patrolling it is a random floor cell within PATROL_RANGE of the enemy, and a
// retry loop is bounded so an enemy boxed in by walls still gets a plan.
static bool choose_target(enemy_t* e, int grace_x, int grace_y) {
    if (e->mode == ENEMY_CHASE || e->mode == ENEMY_ATTACK) {
        if (pathfind_find(e->cell_x, e->cell_y, grace_x, grace_y, &e->path) && (e->path.len > 1)) {
            e->target_x = grace_x;
            e->target_y = grace_y;
            return true;
        }
        // unreachable this step: fall through and wander instead of freezing
    }
    for (int attempt = 0; attempt < 12; attempt++) {
        const int x = e->cell_x + rand_range(-PATROL_RANGE, PATROL_RANGE + 1);
        const int y = e->cell_y + rand_range(-PATROL_RANGE, PATROL_RANGE + 1);
        if (!in_bounds(x, y) || ((x == e->cell_x) && (y == e->cell_y))) {
            continue;
        }
        if (!pathfind_find(e->cell_x, e->cell_y, x, y, &e->path) || (e->path.len < 2)) {
            continue;
        }
        e->target_x = x;
        e->target_y = y;
        return true;
    }
    return false;
}

// How long to stick with the current plan. Random so an enemy's behaviour is
// not a fixed pattern: sometimes it commits to a route for a while, sometimes
// it changes its mind after one cell.
static void plan_duration(enemy_t* e) {
    switch (rand_range(0, 3)) {
        case 0: e->plan_steps = rand_range(1, 3); break;
        case 1: e->plan_steps = rand_range(3, 8); break;
        default: e->plan_steps = rand_range(8, 16); break;
    }
}

static float speed_for(const enemy_t* e) {
    return (e->mode == ENEMY_PATROL) ? ENEMY_WALK_SPEED : ENEMY_RUN_SPEED;
}

// Slide the enemy towards its next waypoint. Grace's movement is quantised into
// cells but this is not, so an enemy crosses a cell over several frames instead
// of appearing in it: the same eased look as walking.
static void slide(enemy_t* e, float dt) {
    if (!e->moving) {
        return;
    }
    const int waypoint = e->path.cells[e->path_idx];
    const vec2_t center = grid_cell_center(waypoint % GRID_W, waypoint / GRID_W);
    const float dx = center.x - e->pos.x;
    const float dy = center.y - e->pos.y;
    const float step = speed_for(e) * dt;
    if ((fabsf(dx) <= step) && (fabsf(dy) <= step)) {
        e->pos = center;
        e->cell_x = waypoint % GRID_W;
        e->cell_y = waypoint / GRID_W;
        e->path_idx++;
        if (e->path_idx >= e->path.len) {
            e->moving = false;
        }
        return;
    }
    // one axis at a time, to stay on the grid lines like Grace
    if (fabsf(dx) >= fabsf(dy)) {
        e->pos.x += (dx > 0.0f) ? step : -step;
    } else {
        e->pos.y += (dy > 0.0f) ? step : -step;
    }
}

// An attack that lands: Grace dodges on a chance, otherwise she respawns at her
// start cell and the map resets - items back on the ground, bag emptied,
// enemies back to their posts. Returns true when Grace was hit.
static bool resolve_attack(void) {
    const bool dodged = ((float)rand() / (float)RAND_MAX) < DODGE_CHANCE;
    if (dodged) {
        return false;
    }
    player_reset();
    items_init();
    inventory_init();
    enemy_init();
    return true;
}

// True when the enemy shares Grace's cell or stands orthogonally next to her.
static bool touching_player(const enemy_t* e, int grace_x, int grace_y) {
    return manhattan(e->cell_x, e->cell_y, grace_x, grace_y) <= ATTACK_CELLS;
}

static void step_enemy(enemy_t* e, int grace_x, int grace_y) {
    const int distance = manhattan(e->cell_x, e->cell_y, grace_x, grace_y);

    // decide what kind of step this is, from how far away Grace is
    if (distance <= ATTACK_CELLS) {
        e->mode = ENEMY_ATTACK;
    } else if (distance <= NOTICE_CELLS) {
        e->mode = ENEMY_CHASE;
    } else {
        e->mode = ENEMY_PATROL;
    }

    // an adjacent enemy swings before it moves
    if ((e->mode == ENEMY_ATTACK) && touching_player(e, grace_x, grace_y)) {
        resolve_attack();
        return;
    }

    if (!e->moving) {
        if (e->plan_steps <= 0) {
            plan_duration(e);
        }
        if (!choose_target(e, grace_x, grace_y)) {
            e->plan_steps = rand_range(1, 4);
            return;
        }
        e->path_idx = 1;
        e->moving = true;
    }
    e->plan_steps--;

    // Grace moving into the enemy, or swapping cells with it, is an attack
    if (touching_player(e, grace_x, grace_y) && (e->mode != ENEMY_PATROL)) {
        resolve_attack();
    }
}

// Grace's last known position, used to tell whether she moved this frame.
static vec2_t last_player_pos;
static bool last_player_known;

void enemy_update(float dt) {
    const vec2_t p = player_position();
    if (!last_player_known) {
        last_player_pos = p;
        last_player_known = true;
        return;
    }
    // The map is turn-based: an enemy only moves on frames where Grace does.
    const bool grace_moved = ((p.x != last_player_pos.x) || (p.y != last_player_pos.y));
    last_player_pos = p;
    if (!grace_moved) {
        return;
    }
    for (int i = 0; i < ENEMY_COUNT; i++) {
        slide(&enemies[i], dt);
    }
}

int enemy_count(void) {
    return ENEMY_COUNT;
}

void enemy_init(void) {
    last_player_known = false;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        enemy_t* e = &enemies[i];
        e->cell_x = ENEMY_START_X[i];
        e->cell_y = ENEMY_START_Y[i];
        e->pos = grid_cell_center(e->cell_x, e->cell_y);
        e->target_x = e->cell_x;
        e->target_y = e->cell_y;
        e->path_idx = 1;
        e->moving = false;
        e->mode = ENEMY_PATROL;
        // a different starting plan length per enemy, so two enemies in the
        // same place do not move in lockstep
        e->plan_steps = rand_range(1 + i, 10 + i);
    }
}

void enemy_turn(void) {
    int grace_x, grace_y;
    player_cell(&grace_x, &grace_y);
    for (int i = 0; i < ENEMY_COUNT; i++) {
        step_enemy(&enemies[i], grace_x, grace_y);
    }
}

void enemy_notify_player_moved(void) {
    enemy_turn();
}

int enemy_active(void) {
    int n = 0;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        if (enemies[i].mode != ENEMY_PATROL) {
            n++;
        }
    }
    return n;
}

enemy_mode_t enemy_mode(int e) {
    return enemies[e].mode;
}

vec2_t enemy_position(int e) {
    return enemies[e].pos;
}

bool enemy_aggro(void) {
    return enemy_active() > 0;
}

int enemy_cell_x(int e) {
    return enemies[e].cell_x;
}

int enemy_cell_y(int e) {
    return enemies[e].cell_y;
}

float enemy_pos_x(int e) {
    return enemies[e].pos.x;
}

float enemy_pos_y(int e) {
    return enemies[e].pos.y;
}

int enemy_mode_id(int e) {
    return (int)enemies[e].mode;
}
