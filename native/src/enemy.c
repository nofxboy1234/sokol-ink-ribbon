//------------------------------------------------------------------------------
//  enemy.c
//  One stalker that roams the map on A* paths. Its behaviour is a state
//  machine: patrol random nearby cells, stop to look around, chase Grace on
//  sight, smell or sound, search where she was probably heading, smash
//  through breakable doors, catch its breath after a long chase, and freeze
//  after a hit.
//------------------------------------------------------------------------------
#include "enemy.h"
#include "doors.h"
#include "grid.h"
#include "pathfind.h"
#include "sight.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

#define ENEMY_EPSILON 0.02f
// Spotting her takes a clear line within 6.5 cells; once on her trail it keeps
// her in view out to 9 cells and hears her within 5 steps.
#define ENEMY_SIGHT_RADIUS 6.5f
#define ENEMY_TRACK_RADIUS 9.0f
#define ENEMY_SMELL_STEPS 5
#define ENEMY_BREAK_CHASE 50
#define ENEMY_BREAK_SEARCH 35
#define ENEMY_BREAK_PATROL 20
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
// Angry after a hit (counted from when the stun wears off): it hunts harder
// for a while, then snaps back to normal. Still fair: even angry it is
// slower than Grace running (6.5 cells/s), it still gets winded (stamina
// drains at half the rate, same recovery threshold), and it still needs a
// revealed cell to see her from.
#define ENEMY_ANGRY_SECONDS 20.0f
#define ENEMY_ANGRY_SPEED 1.25f        // chase and search speed multiplier
#define ENEMY_ANGRY_DRAIN 0.5f         // stamina drain multiplier while chasing
#define ENEMY_ANGRY_SIGHT_BONUS 2.0f   // extra cells of sight and tracking
#define ENEMY_ANGRY_SMELL_BONUS 2      // extra smell steps
#define ENEMY_ANGRY_HEAR_BONUS 2.0f    // extra cells her footsteps carry
#define ENEMY_ANGRY_LOSE_BONUS 1.5f    // extra seconds before it gives up a chase
#define ENEMY_ANGRY_SWEEPS 3           // extra search sweeps
#define ENEMY_SEARCH_RADIUS 6
#define ENEMY_ANGRY_SEARCH_RADIUS 9
#define ENEMY_ANGRY_SEARCH_STEPS 14

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
    enemy_sense_t sense;
    int door;
    enemy_state_t resume;
    unsigned int rng;
    float flash;  // seconds of damage flashing left (counts down with the stun)
    float angry;  // seconds of anger left (counts down once the stun ends)
} e;

static bool is_angry(void) {
    return e.angry > 0.0f;
}

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
    // Each plan rolls whether it is willing to smash through breakable doors.
    int chance = e.state == ENEMY_CHASE ? ENEMY_BREAK_CHASE : e.state == ENEMY_SEARCH ? ENEMY_BREAK_SEARCH : ENEMY_BREAK_PATROL;
    bool through = (int)(rnd() % 100u) < chance;
    if (!pathfind_within(sx, sy, tx, ty, max_cost, through, &scratch)) {
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
        if (!grid_is_floor(nx, ny) || sight_edge_blocked(nx - e.heading_x, ny - e.heading_y, nx, ny)) {
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
            e.speed = rnd_range(3.4f, 4.4f) * (is_angry() ? ENEMY_ANGRY_SPEED : 1.0f);
            e.unseen = 0.0f;
            e.lose_after = rnd_range(2.2f, 3.2f) + (is_angry() ? ENEMY_ANGRY_LOSE_BONUS : 0.0f);
            e.timer = 0.0f;
            break;
        case ENEMY_SEARCH: {
            e.speed = rnd_range(2.6f, 3.4f) * (is_angry() ? ENEMY_ANGRY_SPEED : 1.0f);
            e.sweeps = 4 + (int)(rnd() % 3u) + (is_angry() ? ENEMY_ANGRY_SWEEPS : 0);
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
        if (grid_is_floor(tx, ty) && pathfind_within(avoid_x, avoid_y, tx, ty, ENEMY_SPAWN_MAX, false, &scratch) &&
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
    e.flash = 0.0f;
    e.angry = 0.0f;
    enter(ENEMY_LOOK);
}

void enemy_seed(unsigned int seed) {
    e.rng = seed ? seed : 1u;
}

// A clear straight line between the two dots: walls and closed doors block it.
static bool line_of_sight(float gx, float gy) {
    return sight_clear((int)floorf(e.x), (int)floorf(e.y), (int)floorf(gx), (int)floorf(gy));
}

// Spotting needs a clear line inside the sight radius, from a cell Grace has
// revealed; once on her trail it keeps her out to the wider track radius.
// Smell follows walkable paths a few steps out, so it rounds corners but not
// walls. Hearing ignores walls and doors and reaches as far as her footsteps
// carry. Smell and hearing work in every direction and from the fog.
static enemy_sense_t senses(float gx, float gy, float noise, bool tracking) {
    float dx = gx - e.x;
    float dy = gy - e.y;
    float d2 = dx * dx + dy * dy;
    float radius = (tracking ? ENEMY_TRACK_RADIUS : ENEMY_SIGHT_RADIUS) + (is_angry() ? ENEMY_ANGRY_SIGHT_BONUS : 0.0f);
    int smell = ENEMY_SMELL_STEPS + (is_angry() ? ENEMY_ANGRY_SMELL_BONUS : 0);
    if (noise > 0.0f && is_angry()) {
        noise += ENEMY_ANGRY_HEAR_BONUS;
    }
    if (grid_is_revealed((int)floorf(e.x), (int)floorf(e.y)) && d2 <= radius * radius && line_of_sight(gx, gy)) {
        return SENSE_SIGHT;
    }
    int cx, cy;
    cell_of(&cx, &cy);
    if (d2 <= (float)(smell * smell) &&
        pathfind_within(cx, cy, (int)floorf(gx), (int)floorf(gy), smell, false, &scratch)) {
        return SENSE_SMELL;
    }
    if (noise > 0.0f && d2 <= noise * noise) {
        return SENSE_HEARING;
    }
    return SENSE_NONE;
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

// A closed door on the next step: a breakable one may get smashed (always,
// if it is locked and the plan went through it), an unlocked one is pushed
// open, anything else ends the walk.
static void cross_door(int ax, int ay, int bx, int by) {
    int door = doors_closed_on_edge(ax, ay, bx, by);
    if (door < 0) {
        return;
    }
    const obj_t* o = &grid_level()->objs[door];
    bool locked = doors_block_edge(ax, ay, bx, by);
    if (o->breakable && (locked || rnd() % 2u == 0u)) {
        e.door = door;
        e.resume = e.state;
        e.state = ENEMY_BREAKING;
        e.timer = rnd_range(1.2f, 2.0f);
    } else if (!locked) {
        doors_open(door);
    } else {
        e.path.count = 0;
    }
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
            if (e.index < e.path.count) {
                cross_door(e.path.x[e.index - 1], e.path.y[e.index - 1], e.path.x[e.index], e.path.y[e.index]);
            }
            return e.index >= e.path.count;
        }
        e.x += dx / dist * step;
        e.y += dy / dist * step;
        return false;
    }
    return true;
}

bool enemy_update(float dt, float gx, float gy, float noise) {
    if (e.flash > 0.0f) {
        e.flash -= dt;
    }
    if (e.state != ENEMY_STUNNED && e.angry > 0.0f) {
        e.angry -= dt;
        if (e.angry < 0.0f) {
            e.angry = 0.0f;
        }
    }
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
    e.sense = e.state == ENEMY_WINDED ? SENSE_NONE : senses(gx, gy, noise, tracking);
    bool sensed = e.sense != SENSE_NONE;
    if (sensed) {
        remember(gx, gy);
        if (e.state != ENEMY_CHASE && e.state != ENEMY_BREAKING && e.stamina > ENEMY_STAMINA_MIN) {
            enter(ENEMY_CHASE);
        }
    }

    switch (e.state) {
        case ENEMY_PATROL:
            if (walk(dt)) {
                enter(ENEMY_LOOK);
            }
            break;
        case ENEMY_BREAKING: {
            // If the door was opened (or broken) mid-swing there is nothing
            // left to smash: stop and walk through instead.
            const obj_t* d = &grid_level()->objs[e.door];
            if (d->open || d->state == DOOR_BROKEN) {
                e.state = e.resume;
                break;
            }
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                doors_break(e.door);
                e.state = e.resume;
            }
            break;
        }
        case ENEMY_LOOK:
        case ENEMY_WINDED:
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                enter(e.state == ENEMY_WINDED ? ENEMY_SEARCH : ENEMY_PATROL);
            }
            break;
        case ENEMY_CHASE:
            e.stamina -= dt * (is_angry() ? ENEMY_ANGRY_DRAIN : 1.0f);
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
                if (!route_random(tx, ty, is_angry() ? ENEMY_ANGRY_SEARCH_RADIUS : ENEMY_SEARCH_RADIUS,
                                  is_angry() ? ENEMY_ANGRY_SEARCH_STEPS : ENEMY_SEARCH_STEPS)) {
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

void enemy_damage(float seconds) {
    enemy_stun(seconds);
    e.flash = seconds;
    e.angry = ENEMY_ANGRY_SECONDS;  // a fresh hit restarts the anger
}

float enemy_angry(void) {
    return e.angry;
}

float enemy_flash(void) {
    return e.flash > 0.0f ? e.flash : 0.0f;
}

// A sound at a cell: if it carries as far as the enemy (walls don't stop it,
// like footsteps), it goes to look. Search heads for the remembered cell and
// then sweeps around it. A stunned, winded or door-smashing enemy ignores it.
bool enemy_hear_noise(int x, int y, float radius) {
    if (e.state == ENEMY_STUNNED || e.state == ENEMY_WINDED || e.state == ENEMY_BREAKING) {
        return false;
    }
    float dx = (float)x + 0.5f - e.x;
    float dy = (float)y + 0.5f - e.y;
    if (dx * dx + dy * dy > radius * radius) {
        return false;
    }
    e.seen_x = x;
    e.seen_y = y;
    e.heading_x = 0;
    e.heading_y = 0;
    enter(ENEMY_SEARCH);
    return true;
}

int enemy_focus_x(void) {
    return e.seen_x;
}

int enemy_focus_y(void) {
    return e.seen_y;
}

void enemy_place(float x, float y) {
    e.x = x;
    e.y = y;
    e.path.count = 0;
    e.index = 0;
}

enemy_sense_t enemy_sense(void) {
    return e.sense;
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
