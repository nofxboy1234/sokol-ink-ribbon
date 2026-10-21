//------------------------------------------------------------------------------
//  enemy.c
//  One stalker that roams the map on A* paths. Its behaviour is a state
//  machine: patrol random nearby cells, stop to look around, chase Grace on
//  sight, smell or sound, search where she was probably heading, smash
//  through breakable doors and lamps, catch its breath after a long chase, and
//  freeze after a hit.
//------------------------------------------------------------------------------
#include "enemy.h"
#include "doors.h"
#include "grid.h"
#include "interact.h"
#include "light.h"
#include "pathfind.h"
#include "sight.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
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
// A bottle's impact: walk there (up to this many steps), look around for a
// moment, then sweep a few cells around the point.
#define ENEMY_INVESTIGATE_STEPS 40
#define ENEMY_INVESTIGATE_LOOK 1.2f
#define ENEMY_INVESTIGATE_SWEEPS 4
#define ENEMY_INVESTIGATE_RADIUS 3
#define ENEMY_INVESTIGATE_SWEEP_STEPS 6

#define ENEMY_LIGHT_BREAK_DIST 1.5f
// Per cell travelled within reach (was: per waypoint step; movement is
// continuous now, so the roll is charged per 1.0 cells of travel --
// the same rate: ~2 cells in reach expected before a smash).
#define ENEMY_LIGHT_BREAK_PCT 50
#define ENEMY_LIGHT_BREAK_MIN 1.0f
#define ENEMY_LIGHT_BREAK_MAX 1.6f

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
    int break_target;
    enemy_state_t resume;
    unsigned int rng;
    float flash;  // seconds of damage flashing left (counts down with the stun)
    float angry;  // seconds of anger left (counts down once the stun ends)
    bool grace_lit;  // she can be seen (lit); otherwise only heard or smelt
    int impact_x, impact_y;  // the bottle being investigated
    bool at_impact;          // reached it, now looking around
    float light_travel;      // cells walked since the last lamp-smash roll
    float hp;                // health left (0: dead)
    float fade;              // seconds of fading left once dead
} e;

_Static_assert(sizeof(e) <= ENEMY_SAVE_BYTES, "enemy save size");

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

// Plans from the cell the enemy's body is in. (It used to plan from
// the waypoint it was heading to, a leftover from centre-to-centre
// hops: with free-angle smoothed legs that waypoint is often already
// behind it, so every chase replan turned it back toward a cell it had
// passed -- a visible reverse-and-snap. The first waypoint is its own
// cell, consumed at once by walk(), which still runs cross_door.)
static bool route_to(int tx, int ty, int max_cost) {
    int sx, sy;
    cell_of(&sx, &sy);
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
        case ENEMY_WAIT:
        case ENEMY_DEAD:
            e.path.count = 0;
            break;
        case ENEMY_INVESTIGATE:
            e.speed = rnd_range(2.8f, 3.4f) * (is_angry() ? ENEMY_ANGRY_SPEED : 1.0f);
            e.at_impact = false;
            e.sweeps = ENEMY_INVESTIGATE_SWEEPS;
            e.timer = 0.0f;
            if (!route_to(e.impact_x, e.impact_y, ENEMY_INVESTIGATE_STEPS)) {
                e.path.count = 0;  // can't get there: look around from here
            }
            break;
        case ENEMY_BREAKING:
            break;
    }
}

void enemy_init(int avoid_x, int avoid_y) {
    enemy_spawn(avoid_x, avoid_y, ENEMY_SPAWN_MAX);
}

void enemy_spawn(int x, int y, int radius) {
    e.rng = (unsigned int)time(NULL) * 2654435761u + 1u;
    int avoid_x = x, avoid_y = y;
    e.x = (float)x + 0.5f;
    e.y = (float)y + 0.5f;
    e.path.count = 0;
    e.index = 0;
    int span = radius * 2 + 1;
    int min = radius < ENEMY_SPAWN_MIN ? radius / 2 : ENEMY_SPAWN_MIN;
    for (int attempt = 0; radius > 0 && attempt < 400; attempt++) {
        int tx = avoid_x + (int)(rnd() % (unsigned)span) - radius;
        int ty = avoid_y + (int)(rnd() % (unsigned)span) - radius;
        if (grid_is_floor(tx, ty) && pathfind_within(avoid_x, avoid_y, tx, ty, radius, false, &scratch) &&
            scratch.count - 1 >= min) {
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
    e.grace_lit = true;
    e.hp = ENEMY_HP;
    e.fade = 0.0f;
    enter(ENEMY_LOOK);
}

void enemy_clear(void) {
    // no enemy in the level: it exists only as a dead, invisible body
    enemy_spawn(0, 0, 0);
    e.hp = 0.0f;
    e.fade = 0.0f;
    enter(ENEMY_DEAD);
}

void enemy_seed(unsigned int seed) {
    e.rng = seed ? seed : 1u;
}

// A clear straight line between the two dots: walls, closed doors and
// obstacles block it.
static bool line_of_sight(float gx, float gy) {
    return sight_clear_eyes((int)floorf(e.x), (int)floorf(e.y), (int)floorf(gx), (int)floorf(gy));
}

// Spotting needs Grace lit (her Lighter, or a level light), a clear line
// (walls, closed doors and obstacles block it) inside the sight radius, from
// a cell Grace has revealed; once on her trail it keeps her out to the wider
// track radius. Smell reaches a few cells and hearing as far as her
// footsteps carry; neither is blocked by anything, and both work in every
// direction, in the dark and from the fog.
static enemy_sense_t senses(float gx, float gy, float noise, bool tracking) {
    float dx = gx - e.x;
    float dy = gy - e.y;
    float d2 = dx * dx + dy * dy;
    float radius = (tracking ? ENEMY_TRACK_RADIUS : ENEMY_SIGHT_RADIUS) + (is_angry() ? ENEMY_ANGRY_SIGHT_BONUS : 0.0f);
    int smell = ENEMY_SMELL_STEPS + (is_angry() ? ENEMY_ANGRY_SMELL_BONUS : 0);
    if (noise > 0.0f && is_angry()) {
        noise += ENEMY_ANGRY_HEAR_BONUS;
    }
    if (e.grace_lit && grid_is_revealed((int)floorf(e.x), (int)floorf(e.y)) && d2 <= radius * radius &&
        line_of_sight(gx, gy)) {
        return SENSE_SIGHT;
    }
    // smell, like hearing, is pure distance: walls, doors and obstacles don't stop it
    if (d2 <= (float)(smell * smell)) {
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
        e.break_target = door;
        e.resume = e.state;
        e.state = ENEMY_BREAKING;
        e.timer = rnd_range(1.2f, 2.0f);
    } else if (!locked) {
        doors_open(door);
    } else {
        e.path.count = 0;
    }
}

// Roll only when a lamp is in reach, so arrivals elsewhere leave the seeded stream alone.
static void pass_light(void) {
    if (e.state == ENEMY_BREAKING) {
        return;
    }
    int lamp = light_breakable_near(e.x, e.y, ENEMY_LIGHT_BREAK_DIST);
    if (lamp < 0 || (int)(rnd() % 100u) >= ENEMY_LIGHT_BREAK_PCT) {
        return;
    }
    e.break_target = lamp;
    e.resume = e.state;
    e.state = ENEMY_BREAKING;
    e.timer = rnd_range(ENEMY_LIGHT_BREAK_MIN, ENEMY_LIGHT_BREAK_MAX);
}

// Continuous locomotion, like Grace's steering: A* still plans the
// grid route, but the enemy walks straight at the farthest waypoint it
// can see (string-pulling with sight_clear_eyes), so legs run at any
// angle instead of axis-aligned centre-to-centre hops. Movement is
// axis-separated against a body square (the player.c rule: only cells
// the square newly overlaps can veto), so it slides along walls and
// door frames instead of sticking or clipping. Speed is the length of
// the normalized step -- a diagonal is never faster than a straight leg.
static bool enemy_cell_passable(int cx, int cy) {
    if (!grid_is_floor(cx, cy) || obstacles_block_cell(cx, cy)) {
        return false;
    }
    // routes never step onto or off a climbable top (the pathfind rule)
    int ex, ey;
    cell_of(&ex, &ey);
    return cell_on_climbable(cx, cy) == cell_on_climbable(ex, ey);
}

static bool enemy_steer_passable(float nx, float ny) {
    const float r = 0.30f;
    int cx0 = (int)floorf(nx - r), cx1 = (int)floorf(nx + r);
    int cy0 = (int)floorf(ny - r), cy1 = (int)floorf(ny + r);
    int px0 = (int)floorf(e.x - r), px1 = (int)floorf(e.x + r);
    int py0 = (int)floorf(e.y - r), py1 = (int)floorf(e.y + r);
    int curx, cury;
    cell_of(&curx, &cury);
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            if (cx >= px0 && cx <= px1 && cy >= py0 && cy <= py1) {
                continue;  // already overlapped: not this move's to cross
            }
            if (!enemy_cell_passable(cx, cy)) {
                return false;
            }
            if ((cx != curx || cy != cury) && (cx == curx || cy == cury)) {
                if (doors_on_edge(curx, cury, cx, cy)
                        ? doors_block_edge(curx, cury, cx, cy)
                        : grid_blocked(curx, cury, cx, cy)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The farthest waypoint worth aiming at: every waypoint up to it must
// be visible from here, and the planned route must not cross a closed
// door in between -- a door is still met (and smashed or pushed) cell
// by cell, exactly as before.
static int smoothed_aim(void) {
    int cx, cy;
    cell_of(&cx, &cy);
    int aim = e.index;
    for (int j = e.index + 1; j < e.path.count; j++) {
        if (doors_closed_on_edge(e.path.x[j - 1], e.path.y[j - 1], e.path.x[j], e.path.y[j]) >= 0) {
            break;
        }
        if (!sight_clear_eyes(cx, cy, e.path.x[j], e.path.y[j])) {
            break;
        }
        aim = j;
    }
    return aim;
}

static bool waypoint_reached(int k, int aim, float aim_x, float aim_y) {
    float wx = (float)e.path.x[k] + 0.5f;
    float wy = (float)e.path.y[k] + 0.5f;
    float dx = wx - e.x;
    float dy = wy - e.y;
    if (k == e.path.count - 1) {
        // the last waypoint is a destination, not a corner to cut:
        // only the settle in walk() (centre snap) completes it
        return dx * dx + dy * dy <= ENEMY_EPSILON * ENEMY_EPSILON;
    }
    if (dx * dx + dy * dy <= 0.45f * 0.45f) {
        return true;
    }
    int cx, cy;
    cell_of(&cx, &cy);
    if (cx == e.path.x[k] && cy == e.path.y[k]) {
        return true;
    }
    if (k < aim) {
        // a smoothed leg can pass a waypoint without entering its cell:
        // it counts once it falls behind the line of travel
        return dx * (aim_x - e.x) + dy * (aim_y - e.y) <= 0.0f;
    }
    return false;
}

// Returns true once the path is used up.
static bool walk(float dt) {
    if (e.index >= e.path.count) {
        return true;
    }
    int aim = smoothed_aim();
    float tx = (float)e.path.x[aim] + 0.5f;
    float ty = (float)e.path.y[aim] + 0.5f;
    float dx = tx - e.x;
    float dy = ty - e.y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = e.speed * dt;
    float ox = e.x;
    float oy = e.y;
    int ecx, ecy;
    cell_of(&ecx, &ecy);
    bool in_final_cell = ecx == e.path.x[aim] && ecy == e.path.y[aim];
    if (aim == e.path.count - 1 && dist <= step + ENEMY_EPSILON) {
        e.x = tx;  // final waypoint: settle exactly on its centre
        e.y = ty;
    } else if (aim == e.path.count - 1 && in_final_cell) {
        // inside the destination cell: glide to its centre at walking
        // speed. (This used to teleport to the centre the moment the
        // body entered the cell -- a jump of up to ~0.7 cells at the
        // end of every leg.) Moving toward its own centre only shrinks
        // the body square's overlap, so no wall check is needed.
        e.x += dx / dist * step;
        e.y += dy / dist * step;
    } else if (dist > 0.0f) {
        float mx = dx / dist * (dist <= step ? dist : step);
        float my = dy / dist * (dist <= step ? dist : step);
        if (mx != 0.0f && enemy_steer_passable(e.x + mx, e.y)) {
            e.x += mx;
        }
        if (my != 0.0f && enemy_steer_passable(e.x, e.y + my)) {
            e.y += my;
        }
        if (e.x == ox && e.y == oy) {
            // boxed in by the smoothed line: fall back to the next
            // waypoint centre, the old centre-to-centre route
            float wx = (float)e.path.x[e.index] + 0.5f;
            float wy = (float)e.path.y[e.index] + 0.5f;
            float vx = wx - e.x;
            float vy = wy - e.y;
            float vd = sqrtf(vx * vx + vy * vy);
            if (vd > 0.0f) {
                float fx = vx / vd * (vd <= step ? vd : step);
                float fy = vy / vd * (vd <= step ? vd : step);
                if (fx != 0.0f && enemy_steer_passable(e.x + fx, e.y)) {
                    e.x += fx;
                }
                if (fy != 0.0f && enemy_steer_passable(e.x, e.y + fy)) {
                    e.y += fy;
                }
            }
        }
    }
    // lamp-smash rolls are charged per cell travelled (see the define)
    float moved = sqrtf((e.x - ox) * (e.x - ox) + (e.y - oy) * (e.y - oy));
    if (!(e.light_travel >= 0.0f && e.light_travel < 1.0f)) {
        e.light_travel = 0.0f;  // a pre-change save blob can't be trusted here
    }
    e.light_travel += moved;
    while (e.light_travel >= 1.0f) {
        e.light_travel -= 1.0f;
        pass_light();
        if (e.state == ENEMY_BREAKING) {
            return false;
        }
    }
    // consume every waypoint this leg has reached, with the same
    // per-step bookkeeping as the old centre-to-centre walk
    while (e.index < e.path.count && waypoint_reached(e.index, aim, tx, ty)) {
        int k = e.index;
        e.index++;
        if (e.index < e.path.count) {
            if (e.state == ENEMY_BREAKING) {
                break;
            }
            cross_door(e.path.x[k], e.path.y[k], e.path.x[e.index], e.path.y[e.index]);
            if (e.state == ENEMY_BREAKING || e.path.count == 0) {
                break;
            }
        }
    }
    return e.index >= e.path.count;
}

bool enemy_update(float dt, float gx, float gy, float noise) {
    if (e.state == ENEMY_DEAD) {
        if (e.fade > 0.0f) e.fade -= dt;
        return false;
    }
    if (e.flash > 0.0f) {
        e.flash -= dt;
    }
    if (e.state != ENEMY_STUNNED && e.angry > 0.0f) {
        e.angry -= dt;
        if (e.angry < 0.0f) {
            e.angry = 0.0f;
        }
    }
    if (e.state == ENEMY_STUNNED || e.state == ENEMY_WAIT) {
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
    // a bottle comes first: only seeing Grace herself pulls it off
    bool busy = e.state == ENEMY_INVESTIGATE && e.sense != SENSE_SIGHT;
    if (sensed && !busy) {
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
            const obj_t* t = &grid_level()->objs[e.break_target];
            if (t->kind == OBJ_LIGHT) {
                if (!light_is_breakable_now(t)) {
                    e.state = e.resume;
                    break;
                }
            } else if (t->open || t->state == DOOR_BROKEN) {
                e.state = e.resume;
                break;
            }
            e.timer -= dt;
            if (e.timer <= 0.0f) {
                if (t->kind == OBJ_LIGHT) {
                    light_break(e.break_target);
                } else {
                    doors_break(e.break_target);
                }
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
        case ENEMY_INVESTIGATE:
            if (e.timer > 0.0f) {
                e.timer -= dt;
                break;
            }
            if (walk(dt)) {
                if (!e.at_impact) {
                    e.at_impact = true;
                    e.timer = ENEMY_INVESTIGATE_LOOK;  // stop and look at it
                    break;
                }
                if (e.sweeps-- <= 0) {
                    enter(ENEMY_PATROL);
                    break;
                }
                e.timer = rnd_range(0.3f, 0.8f);
                if (!route_random(e.impact_x, e.impact_y, ENEMY_INVESTIGATE_RADIUS, ENEMY_INVESTIGATE_SWEEP_STEPS)) {
                    e.path.count = 0;
                }
            }
            break;
        case ENEMY_STUNNED:
        case ENEMY_WAIT:
        case ENEMY_DEAD:
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
    if (e.state == ENEMY_DEAD) return;
    enter(ENEMY_STUNNED);
    e.timer = seconds;
}

void enemy_damage(float seconds) {
    if (e.state == ENEMY_DEAD) return;
    enemy_stun(seconds);
    e.flash = seconds;
    e.angry = ENEMY_ANGRY_SECONDS;  // a fresh hit restarts the anger
}

bool enemy_hurt(float amount, bool critical) {
    if (e.state == ENEMY_DEAD) return false;
    e.hp -= critical ? e.hp : amount;
    if (e.hp <= 0.0f) {
        e.hp = 0.0f;
        enter(ENEMY_DEAD);
        e.fade = ENEMY_FADE_SECONDS;
        e.flash = 0.0f;
        e.angry = 0.0f;
        return true;
    }
    enemy_damage(ENEMY_STAGGER_SECONDS);
    return false;
}

float enemy_hp(void) { return e.hp; }
bool enemy_alive(void) { return e.state != ENEMY_DEAD; }

float enemy_alpha(void) {
    if (e.state != ENEMY_DEAD) return 1.0f;
    return e.fade > 0.0f ? e.fade / ENEMY_FADE_SECONDS : 0.0f;
}

float enemy_angry(void) {
    return e.angry;
}

float enemy_flash(void) {
    return e.flash > 0.0f ? e.flash : 0.0f;
}

// A sound at a cell: if it carries as far as the enemy (walls don't stop it,
// like footsteps), it goes to look. Search heads for the remembered cell and
// then sweeps around it.
bool enemy_hear_noise(int x, int y, float radius) {
    if (e.state == ENEMY_DEAD) return false;
    if (e.state == ENEMY_STUNNED || e.state == ENEMY_WINDED || e.state == ENEMY_BREAKING || e.state == ENEMY_WAIT) {
        return false;
    }
    if (e.state == ENEMY_CHASE && e.sense == SENSE_SIGHT) {
        return false;  // it has her in sight: the bottle doesn't distract it
    }
    float dx = (float)x + 0.5f - e.x;
    float dy = (float)y + 0.5f - e.y;
    if (dx * dx + dy * dy > radius * radius) {
        return false;
    }
    e.impact_x = x;
    e.impact_y = y;
    e.seen_x = x;
    e.seen_y = y;
    e.heading_x = 0;
    e.heading_y = 0;
    enter(ENEMY_INVESTIGATE);
    return true;
}

void enemy_rest(float seconds) {
    if (e.state == ENEMY_DEAD) return;
    enter(ENEMY_WAIT);
    e.timer = seconds;
}

void enemy_set_grace_lit(bool lit) {
    e.grace_lit = lit;
}

void enemy_export(enemy_save_t* out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->bytes, &e, sizeof(e));
}

void enemy_import(const enemy_save_t* in) {
    memcpy(&e, in->bytes, sizeof(e));
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
