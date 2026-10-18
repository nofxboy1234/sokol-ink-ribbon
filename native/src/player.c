#include "player.h"
#include "pathfind.h"
#include "grid.h"
#include "level.h"
#include "doors.h"
#include "interact.h"

#include <math.h>

#define PLAYER_WALK_SPEED PLAYER_WALK_SPEED_CELLS
#define PLAYER_RUN_SPEED PLAYER_RUN_SPEED_CELLS
#define PLAYER_EPSILON 0.02f
#define REPLAY_MAX 8192

static struct {
    float x, y;
    path_t path;
    int index;
    player_move_t mode;
    int steps;
    bool moving;
    bool waiting;
    int facing;
    int replay_len;
    int replay_x[REPLAY_MAX];
    int replay_y[REPLAY_MAX];
    int replay_t[REPLAY_MAX];
    int clock_ms;
    bool on_top;
    float vx, vy;
    bool input_active;
    int last_replay_ms;
    int last_cell_x, last_cell_y;
    float climb_timer;
    bool climbing;
    int climb_x, climb_y;
} p;

static bool on_top_at(int cx, int cy) {
    const level_t* lv = grid_level();
    if (!lv) return false;
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind == OBJ_OBSTACLE && o->climbable && cx >= o->x && cx < o->x + (o->w > 0 ? o->w : 1) && cy >= o->y && cy < o->y + (o->h > 0 ? o->h : 1)) return true;
    }
    return false;
}

static void replay_record(void) {
    if (p.replay_len >= REPLAY_MAX) {
        return;
    }
    p.replay_x[p.replay_len] = (int)floorf(p.x);
    p.replay_y[p.replay_len] = (int)floorf(p.y);
    p.replay_t[p.replay_len] = p.clock_ms;
    p.replay_len++;
}

static bool steer_cell_passable(int cx, int cy) {
    if (!grid_is_floor(cx, cy) || obstacles_block_cell(cx, cy)) {
        return false;
    }
    // never steer onto or off a climbable: climbing stays a prompted action
    return p.on_top ? on_top_at(cx, cy) : !on_top_at(cx, cy);
}

static bool steer_pos_passable(float nx, float ny) {
    const float r = 0.30f;
    int cx0 = (int)floorf(nx - r), cx1 = (int)floorf(nx + r);
    int cy0 = (int)floorf(ny - r), cy1 = (int)floorf(ny + r);
    // The cells her square already overlaps at the current position must
    // not veto the move: hugging a door frame, her square straddles the
    // wall line, and the old all-cells check then blocked sliding along
    // it or moving away -- she could be stuck fast on the side of a
    // door. Only cells the candidate square newly overlaps can block it.
    int px0 = (int)floorf(p.x - r), px1 = (int)floorf(p.x + r);
    int py0 = (int)floorf(p.y - r), py1 = (int)floorf(p.y + r);
    int curx = (int)floorf(p.x), cury = (int)floorf(p.y);
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            if (cx >= px0 && cx <= px1 && cy >= py0 && cy <= py1) {
                continue;  // already overlapped: not this move's to cross
            }
            if (!steer_cell_passable(cx, cy)) {
                return false;
            }
            // a wall or door on the edge she is actually crossing still blocks
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

void player_set_velocity(float vx, float vy) {
    p.vx = vx;
    p.vy = vy;
    p.input_active = vx != 0.0f || vy != 0.0f;
    if (p.input_active) {
        p.moving = false;  // steering replaces any path route
        p.path.count = 0;
        p.waiting = false;
        float speed = sqrtf(vx * vx + vy * vy);
        p.mode = speed > (PLAYER_WALK_SPEED + PLAYER_RUN_SPEED) * 0.5f ? PLAYER_RUN : PLAYER_WALK;
    }
}

bool player_steering(void) {
    return p.input_active;
}

void player_init(int x, int y) {
    p.x = (float)x + 0.5f;
    p.y = (float)y + 0.5f;
    p.path.count = 0;
    p.index = 0;
    p.mode = PLAYER_WALK;
    p.steps = 0;
    p.moving = false;
    p.waiting = false;
    p.facing = 0;
    p.replay_len = 0;
    p.clock_ms = 0;
    p.climb_timer = 0.0f;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.input_active = false;
    p.last_replay_ms = 0;
    p.last_cell_x = x;
    p.last_cell_y = y;
    p.on_top = on_top_at(x, y);
    replay_record();
}

void player_set_cell(int x, int y) {
    player_init(x, y);
}

// Place her at an exact sub-cell position (tests, save/load settling).
void player_set_position(float x, float y) {
    p.x = x;
    p.y = y;
    p.last_cell_x = (int)floorf(x);
    p.last_cell_y = (int)floorf(y);
    p.on_top = on_top_at(p.last_cell_x, p.last_cell_y);
}

const path_t* player_path(void) {
    return &p.path;
}

void player_move_to(int x, int y, player_move_t mode) {
    int cx, cy;
    player_cell(&cx, &cy);
    if (!pathfind(cx, cy, x, y, &p.path) || p.path.count < 2) {
        p.moving = false;
        return;
    }
    p.index = 1;
    p.mode = mode;
    p.moving = true;
    p.waiting = false;
}

void player_stop(void) {
    p.moving = false;
    p.path.count = 0;
    p.index = 0;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.input_active = false;
}

void player_halt(void) {
    player_stop();
    p.x = floorf(p.x) + 0.5f;
    p.y = floorf(p.y) + 0.5f;
}

void player_note_time(int ms) { p.clock_ms = ms; }
bool player_climbing(void) { return p.climbing; }
void player_climb_to(int x, int y) {
    if (p.climbing) return;
    p.climbing = true;
    p.climb_x = x;
    p.climb_y = y;
    p.climb_timer = 0.35f;
    p.moving = false;
}
bool player_on_top(void) { return p.on_top; }
int player_replay_t(int index) { return index >= 0 && index < p.replay_len ? p.replay_t[index] : 0; }
void player_update(float dt) {
    if (p.climbing) {
        p.climb_timer -= dt;
        if (p.climb_timer <= 0.0f) {
            p.climbing = false;
            p.x = (float)p.climb_x + 0.5f;
            p.y = (float)p.climb_y + 0.5f;
            p.on_top = on_top_at(p.climb_x, p.climb_y);
            p.steps++;
            replay_record();
        }
        return;
    }
    if (p.input_active) {
        // axis-separated steering: slide along walls instead of sticking
        if (p.vx != 0.0f) {
            float nx = p.x + p.vx * dt;
            if (steer_pos_passable(nx, p.y)) {
                p.x = nx;
            }
        }
        if (p.vy != 0.0f) {
            float ny = p.y + p.vy * dt;
            if (steer_pos_passable(p.x, ny)) {
                p.y = ny;
            }
        }
        if (fabsf(p.vx) > fabsf(p.vy)) {
            p.facing = p.vx > 0.0f ? 1 : 3;
        } else if (p.vy != 0.0f) {
            p.facing = p.vy > 0.0f ? 2 : 0;
        }
        int cx, cy;
        player_cell(&cx, &cy);
        if (cx != p.last_cell_x || cy != p.last_cell_y) {
            p.last_cell_x = cx;
            p.last_cell_y = cy;
            p.steps++;
            p.on_top = on_top_at(cx, cy);
            replay_record();
            p.last_replay_ms = p.clock_ms;
        } else if (p.clock_ms - p.last_replay_ms >= 100) {
            // free movement crosses cells diagonally: sample the replay so
            // playback stays smooth instead of hopping between cell centres
            replay_record();
            p.last_replay_ms = p.clock_ms;
        }
        return;
    }
    if (!p.moving || p.index >= p.path.count) {
        p.moving = false;
        return;
    }
    float speed = (p.mode == PLAYER_RUN) ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED;
    float tx = (float)p.path.x[p.index] + 0.5f;
    float ty = (float)p.path.y[p.index] + 0.5f;
    float dx = tx - p.x;
    float dy = ty - p.y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = speed * dt;
    if (dist <= step + PLAYER_EPSILON) {
        p.x = tx;
        p.y = ty;
        p.steps++;
        replay_record();
        p.on_top = on_top_at(p.path.x[p.index], p.path.y[p.index]);
        p.index++;
        if (p.index >= p.path.count) {
            p.moving = false;
        }
        return;
    }
    p.x += dx / dist * step;
    p.y += dy / dist * step;
    if (fabsf(dx) > fabsf(dy)) {
        p.facing = dx > 0.0f ? 1 : 3;
    } else {
        p.facing = dy > 0.0f ? 2 : 0;
    }
}

float player_x(void) {
    return p.x;
}

float player_y(void) {
    return p.y;
}

void player_cell(int* x, int* y) {
    if (x) {
        *x = (int)floorf(p.x);
    }
    if (y) {
        *y = (int)floorf(p.y);
    }
}

int player_steps_taken(void) {
    return p.steps;
}

bool player_is_moving(void) {
    return p.moving || p.input_active;
}

bool player_is_waiting(void) {
    return p.waiting;
}

void player_toggle_wait(void) {
    p.waiting = !p.waiting;
    if (p.waiting) {
        player_stop();
    }
}

player_move_t player_move_mode(void) {
    return p.mode;
}

int player_facing(void) {
    return p.facing;
}

int player_replay_length(void) {
    return p.replay_len;
}

int player_replay_x(int index) {
    if (index < 0 || index >= p.replay_len) {
        return -1;
    }
    return p.replay_x[index];
}

int player_replay_y(int index) {
    if (index < 0 || index >= p.replay_len) {
        return -1;
    }
    return p.replay_y[index];
}
