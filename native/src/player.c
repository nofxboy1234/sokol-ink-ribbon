#include "player.h"
#include "pathfind.h"

#include <math.h>

#define PLAYER_WALK_SPEED 3.0f
#define PLAYER_RUN_SPEED 6.5f
#define PLAYER_EPSILON 0.02f

static struct {
    float x, y;
    path_t path;
    int index;
    player_move_t mode;
    int steps;
    bool moving;
    bool waiting;
    int facing;
} p;

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
}

void player_update(float dt) {
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
    return p.moving;
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
