//------------------------------------------------------------------------------
//  enemy.c
//  One stalker that roams the map on A* paths. Its behaviour is a state
//  machine: patrol random nearby cells, stop to look around, chase Grace on
//  sight, search where she was probably heading, catch its breath after a
//  long chase, and freeze after a hit.
//------------------------------------------------------------------------------
#include "enemy.h"
#include "doors.h"
#include "grid.h"
#include "pathfind.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

#define ENEMY_EPSILON 0.02f
// Spotting her takes a clear line within 6.5 cells; once on her trail it keeps
// her in view out to 9 cells and hears her within 5 steps.
#define ENEMY_SIGHT_RADIUS 6.5f
#define ENEMY_TRACK_RADIUS 9.0f
#define ENEMY_HEAR_STEPS 5
#define ENEMY_TOUCH_DIST 0.95f
#define ENEMY_REPLAN 0.35f
#define ENEMY_CHASE_STEPS 30
#define ENEMY_PATROL_STEPS 14
#define ENEMY_SEARCH_STEPS 9
#define ENEMY_PREDICT_STEPS 4
#define ENEMY_SPAWN_MIN 10
#define ENEMY_SPAWN_MAX 18
// Seconds of all-out pursuit before it has to stop and catch its breath.
#define ENEMY_STAMINA_MAX 12.0f
#define ENEMY_STAMINA_MIN 4.0f
#define ENEMY_STAMINA_REGEN 1.5f

static struct {
    enemy_state_t state;
    float x, y;
    path_t path;
    int index;
    float speed;
    float timer;
    float unseen;
    float lose_after;
    float stamina;
    int sweeps;
    int seen_x, seen_y;
    int heading_x, heading_y;
    unsigned int rng;
} e;

static path_t scratch;

static bool edge_blocked(int ax, int ay, int bx, int by) {
    if (!grid_is_floor(bx, by)) {
        return true;
    }
    return doors_on_edge(ax, ay, bx, by) ? doors_block_edge(ax, ay, bx, by) : grid_blocked(ax, ay, bx, by);
}

static unsigned int rnd(void) {
    e.rng ^= e.rng << 13;
    e.rng ^= e.rng >> 17;
    e.rng ^= e.rng << 5;
    return e.rng;
}

static float rnd_range(float lo, float hi) {
    return lo + (hi - lo) * (float)(rnd() % 10000u) / 10000.0f;
}

static void cell_of(int* cx, int* cy) {
    *cx = (int)floorf(e.x);
    *cy = (int)floorf(e.y);
}

// Plans from the waypoint it is already heading to, so a replan never snaps back.
static bool route_to(int tx, int ty, int max_cost) {
    int sx, sy;
    if (e.index < e.path.count) {
        sx = e.path.x[e.index];
        sy = e.path.y[e.index];
    } else {
        cell_of(&sx, &sy);
    }
    if (!pathfind_within(sx, sy, tx, ty, max_cost, &scratch)) {
        return false;
    }
    e.path = scratch;
    e.index = 0;
    return true;
}

static bool route_random(int cx, int cy, int radius, int max_cost) {
    for (int attempt = 0; attempt < 12; attempt++) {
        int tx = cx + (int)(rnd() % (unsigned)(radius * 2 + 1)) - radius;
        int ty = cy + (int)(rnd() % (unsigned)(radius * 2 + 1)) - radius;
        if (grid_is_floor(tx, ty) && route_to(tx, ty, max_cost) && e.path.count > 1) {
            return true;
        }
    }
    return false;
}

// Where Grace was probably going: a few steps along her last heading.
static void predicted_cell(int* tx, int* ty) {
    *tx = e.seen_x;
    *ty = e.seen_y;
    for (int i = 1; i <= ENEMY_PREDICT_STEPS; i++) {
        int nx = e.seen_x + e.heading_x * i;
        int ny = e.seen_y + e.heading_y * i;
        if (!grid_is_floor(nx, ny) || edge_blocked(nx - e.heading_x, ny - e.heading_y, nx, ny)) {
            break;
        }
        *tx = nx;
        *ty = ny;
    }
}

static void enter(enemy_state_t state) {
    int cx, cy;
    cell_of(&cx, &cy);
    e.state = state;
    switch (state) {
        case ENEMY_PATROL:
            // now and then it breaks into a short jog to stay unpredictable
            e.speed = (rnd() % 4u == 0u) ? rnd_range(3.2f, 3.8f) : rnd_range(1.6f, 2.4f);
            if (!route_random(cx, cy, 8, ENEMY_PATROL_STEPS)) {
                e.state = ENEMY_LOOK;
                e.timer = rnd_range(0.5f, 1.5f);
            }
            break;
        case ENEMY_LOOK:
            e.path.count = 0;
            e.timer = rnd_range(0.6f, 2.4f);
            break;
        case ENEMY_CHASE:
            e.speed = rnd_range(3.4f, 4.4f);
            e.unseen = 0.0f;
            e.lose_after = rnd_range(2.2f, 3.2f);
            e.timer = 0.0f;
            break;
        case ENEMY_SEARCH: {
            e.speed = rnd_range(2.6f, 3.4f);
            e.sweeps = 4 + (int)(rnd() % 3u);
            int tx, ty;
            predicted_cell(&tx, &ty);
            if (!route_to(tx, ty, ENEMY_CHASE_STEPS) && !route_to(e.seen_x, e.seen_y, ENEMY_CHASE_STEPS)) {
                e.path.count = 0;
            }
            e.timer = 0.0f;
            break;
        }
        case ENEMY_WINDED:
            e.path.count = 0;
            e.timer = rnd_range(2.0f, 3.0f);
            break;
        case ENEMY_STUNNED:
            e.path.count = 0;
            break;
    }
}

void enemy_init(int avoid_x, int avoid_y) {
    e.rng = (unsigned int)time(NULL) * 2654435761u + 1u;
    e.x = (float)avoid_x + 0.5f;
    e.y = (float)avoid_y + 0.5f;
    e.path.count = 0;
    e.index = 0;
    for (int attempt = 0; attempt < 400; attempt++) {
        int tx = avoid_x + (int)(rnd() % 37u) - 18;
        int ty = avoid_y + (int)(rnd() % 37u) - 18;
        if (grid_is_floor(tx, ty) && pathfind_within(avoid_x, avoid_y, tx, ty, ENEMY_SPAWN_MAX, &scratch) &&
            scratch.count - 1 >= ENEMY_SPAWN_MIN) {
            e.x = (float)tx + 0.5f;
            e.y = (float)ty + 0.5f;
            break;
        }
    }
    e.seen_x = avoid_x;
    e.seen_y = avoid_y;
    e.heading_x = 0;
    e.heading_y = 0;
    e.stamina = ENEMY_STAMINA_MAX;
    enter(ENEMY_LOOK);
}

// A clear straight line between the two dots: walls and closed doors block it.
static bool line_of_sight(float gx, float gy) {
    int x = (int)floorf(e.x);
    int y = (int)floorf(e.y);
    int tx = (int)floorf(gx);
    int ty = (int)floorf(gy);
    int dx = abs(tx - x);
    int dy = -abs(ty - y);
    int sx = x < tx ? 1 : -1;
    int sy = y < ty ? 1 : -1;
    int err = dx + dy;
    while (x != tx || y != ty) {
        int e2 = 2 * err;
        int nx = x;
        int ny = y;
        if (e2 >= dy) {
            err += dy;
            nx += sx;
        }
        if (e2 <= dx) {
            err += dx;
            ny += sy;
        }
        if (nx != x && ny != y) {
            bool via_x = !edge_blocked(x, y, nx, y) && !edge_blocked(nx, y, nx, ny);
            bool via_y = !edge_blocked(x, y, x, ny) && !edge_blocked(x, ny, nx, ny);
            if (!via_x && !via_y) {
                return false;
            }
        } else if (edge_blocked(x, y, nx, ny)) {
            return false;
        }
        x = nx;
        y = ny;
    }
    return true;
}

// Spotting needs a clear line inside the sight radius, from a cell Grace has
// revealed. Once on her trail it keeps her out to the wider track radius and
// hears her footsteps a few steps away, even round a corner.
static bool senses(float gx, float gy, bool tracking) {
    if (!grid_is_revealed((int)floorf(e.x), (int)floorf(e.y))) {
        return false;
    }
    float dx = gx - e.x;
    float dy = gy - e.y;
    float radius = tracking ? ENEMY_TRACK_RADIUS : ENEMY_SIGHT_RADIUS;
    if (dx * dx + dy * dy <= radius * radius && line_of_sight(gx, gy)) {
        return true;
    }
    if (!tracking) {
        return false;
    }
    int cx, cy;
    cell_of(&cx, &cy);
    return pathfind_within(cx, cy, (int)floorf(gx), (int)floorf(gy), ENEMY_HEAR_STEPS, &scratch);
}

static void remember(float gx, float gy) {
    int x = (int)floorf(gx);
    int y = (int)floorf(gy);
    if (x != e.seen_x || y != e.seen_y) {
        e.heading_x = (x > e.seen_x) - (x < e.seen_x);
        e.heading_y = (y > e.seen_y) - (y < e.seen_y);
        if (e.heading_x != 0 && e.heading_y != 0) {
            e.heading_y = 0;
        }
    }
    e.seen_x = x;
    e.seen_y = y;
}

// Returns true once the path is used up.
static bool walk(float dt) {
    if (e.index < e.path.count) {
        float tx = (float)e.path.x[e.index] + 0.5f;
        float ty = (float)e.path.y[e.index] + 0.5f;
        float dx = tx - e.x;
        float dy = ty - e.y;
        float dist = sqrtf(dx * dx + dy * dy);
        float step = e.speed * dt;
        if (dist <= step + ENEMY_EPSILON) {
            e.x = tx;
            e.y = ty;
            e.index++;
            return e.index >= e.path.count;
        }
        e.x += dx / dist * step;
        e.y += dy / dist * step;
        return false;
    }
    return true;
}

bool enemy_update(float dt, float gx, float gy) {
    if (e.state == ENEMY_STUNNED) {
        e.timer -= dt;
        if (e.timer <= 0.0f) {
            enter(ENEMY_SEARCH);
        }
        return false;
    }
    if (e.state != ENEMY_CHASE && e.stamina < ENEMY_STAMINA_MAX) {
        e.stamina += dt * ENEMY_STAMINA_REGEN;
    }

    bool tracking = e.state == ENEMY_CHASE || e.state == ENEMY_SEARCH;
    bool sensed = e.state != ENEMY_WINDED && senses(gx, gy, tracking);
    if (sensed) {
        remember(gx, gy);
        if (e.state != ENEMY_CHASE && e.stamina > ENEMY_STAMINA_MIN) {
            enter(ENEMY_CHASE);
        }
    }

    switch (e.state) {
        case ENEMY_PATROL:
            if (walk(dt)) {
                enter(ENEMY_LOOK);
            }
            break;
        case ENEMY_LOOK:
        case ENEMY_WINDED:
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                enter(e.state == ENEMY_WINDED ? ENEMY_SEARCH : ENEMY_PATROL);
            }
            break;
        case ENEMY_CHASE:
            e.stamina -= dt;
            if (e.stamina <= 0.0f) {
                e.stamina = 0.0f;
                enter(ENEMY_WINDED);
                break;
            }
            e.unseen = sensed ? 0.0f : e.unseen + dt;
            if (e.unseen > e.lose_after) {
                enter(ENEMY_SEARCH);
                break;
            }
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                e.timer = ENEMY_REPLAN;
                int tx = e.seen_x;
                int ty = e.seen_y;
                if (!sensed) {
                    predicted_cell(&tx, &ty);
                }
                route_to(tx, ty, ENEMY_CHASE_STEPS);
            }
            walk(dt);
            break;
        case ENEMY_SEARCH:
            if (e.timer > 0.0f) {
                e.timer -= dt;
                break;
            }
            if (walk(dt)) {
                if (e.sweeps-- <= 0) {
                    enter(ENEMY_PATROL);
                    break;
                }
                e.timer = rnd_range(0.3f, 1.0f);
                int tx, ty;
                predicted_cell(&tx, &ty);
                if (!route_random(tx, ty, 6, ENEMY_SEARCH_STEPS)) {
                    e.path.count = 0;
                }
            }
            break;
        case ENEMY_STUNNED:
            break;
    }

    if (!grid_is_revealed((int)floorf(e.x), (int)floorf(e.y))) {
        return false;
    }
    float dx = gx - e.x;
    float dy = gy - e.y;
    return dx * dx + dy * dy < ENEMY_TOUCH_DIST * ENEMY_TOUCH_DIST;
}

void enemy_stun(float seconds) {
    enter(ENEMY_STUNNED);
    e.timer = seconds;
}

enemy_state_t enemy_state(void) {
    return e.state;
}

float enemy_x(void) {
    return e.x;
}

float enemy_y(void) {
    return e.y;
}
