//------------------------------------------------------------------------------
//  enemy.c
//  One stalker that roams the map on A* paths. Its behaviour is a state
//  machine: patrol random nearby cells, stop to look around, chase Grace on
//  sight, search where she was last seen, and freeze after a hit.
//------------------------------------------------------------------------------
#include "enemy.h"
#include "grid.h"
#include "pathfind.h"

#include <math.h>
#include <time.h>

#define ENEMY_EPSILON 0.02f
#define ENEMY_SIGHT_RADIUS 6.5f
#define ENEMY_SIGHT_STEPS 9
#define ENEMY_TOUCH_DIST 0.95f
#define ENEMY_LOSE_SIGHT 1.2f
#define ENEMY_REPLAN 0.35f
#define ENEMY_PATROL_STEPS 14
#define ENEMY_SEARCH_STEPS 5
#define ENEMY_SPAWN_MIN 10
#define ENEMY_SPAWN_MAX 18

static struct {
    enemy_state_t state;
    float x, y;
    path_t path;
    int index;
    float speed;
    float timer;
    float unseen;
    int sweeps;
    int seen_x, seen_y;
    unsigned int rng;
} e;

static path_t scratch;

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
            e.timer = 0.0f;
            break;
        case ENEMY_SEARCH:
            e.speed = rnd_range(2.4f, 3.2f);
            e.sweeps = 2 + (int)(rnd() % 3u);
            if (!route_to(e.seen_x, e.seen_y, ENEMY_PATROL_STEPS * 2)) {
                e.path.count = 0;
            }
            e.timer = 0.0f;
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
    enter(ENEMY_LOOK);
}

static bool sees(float gx, float gy) {
    float dx = gx - e.x;
    float dy = gy - e.y;
    if (dx * dx + dy * dy > ENEMY_SIGHT_RADIUS * ENEMY_SIGHT_RADIUS) {
        return false;
    }
    int cx, cy;
    cell_of(&cx, &cy);
    return pathfind_within(cx, cy, (int)floorf(gx), (int)floorf(gy), ENEMY_SIGHT_STEPS, &scratch);
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

    bool seen = sees(gx, gy);
    if (seen) {
        e.seen_x = (int)floorf(gx);
        e.seen_y = (int)floorf(gy);
        if (e.state != ENEMY_CHASE) {
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
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                enter(ENEMY_PATROL);
            }
            break;
        case ENEMY_CHASE:
            e.unseen = seen ? 0.0f : e.unseen + dt;
            if (e.unseen > ENEMY_LOSE_SIGHT) {
                enter(ENEMY_SEARCH);
                break;
            }
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                e.timer = ENEMY_REPLAN;
                route_to(e.seen_x, e.seen_y, ENEMY_SIGHT_STEPS * 2);
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
                e.timer = rnd_range(0.3f, 1.1f);
                if (!route_random(e.seen_x, e.seen_y, 4, ENEMY_SEARCH_STEPS)) {
                    e.path.count = 0;
                }
            }
            break;
        case ENEMY_STUNNED:
            break;
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
