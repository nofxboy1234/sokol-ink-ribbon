#include "player.h"
#include "grid.h"
#include "pathfind.h"
#include <stdbool.h>
#include <math.h>

// Grace walks at half her running speed (map units per second)
#define PLAYER_WALK_SPEED (CELL * 2.0f)
#define PLAYER_RUN_SPEED (CELL * 4.0f)

// Longest slice of a frame that movement is integrated in, in seconds. One
// 60fps frame is enough resolution: even at running speed that is a few map
// units per slice, far short of the distance to the next cell centre, so a
// step can never overshoot a waypoint and snapping stays exact.
#define MAX_SUB_STEP (1.0f / 60.0f)

static struct {
    float x, y;
    int cell_x, cell_y;
    path_t path;
    int path_idx;
    bool moving;
    // set by player_stop: halt on reaching the next cell rather than mid-edge
    bool stop_at_next_cell;
    float speed;
    int steps;
} p;

static int start_x, start_y;

static float speed_for(player_move_t mode) {
    return (mode == PLAYER_MOVE_RUN) ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED;
}

void player_init(int cell_x, int cell_y) {
    start_x = cell_x;
    start_y = cell_y;
    p.cell_x = cell_x;
    p.cell_y = cell_y;
    const vec2_t center = grid_cell_center(cell_x, cell_y);
    p.x = center.x;
    p.y = center.y;
    p.moving = false;
    p.stop_at_next_cell = false;
    p.steps = 0;
}

void player_reset(void) {
    const vec2_t center = grid_cell_center(start_x, start_y);
    p.cell_x = start_x;
    p.cell_y = start_y;
    p.x = center.x;
    p.y = center.y;
    p.moving = false;
    p.stop_at_next_cell = false;
    p.path_idx = 1;
}

void player_stop(void) {
    // Halting mid-edge would leave Grace between cells, which breaks the
    // grid-aligned look, so she walks out the rest of the edge first.
    p.stop_at_next_cell = p.moving;
}

// the cell Grace is currently heading to: the next waypoint while moving,
// otherwise the cell she already stands on
static void current_target_cell(int* cx, int* cy) {
    if (p.moving) {
        const int waypoint = p.path.cells[p.path_idx];
        *cx = waypoint % GRID_W;
        *cy = waypoint / GRID_W;
    } else {
        *cx = p.cell_x;
        *cy = p.cell_y;
    }
}

void player_move_to(int cell_x, int cell_y, player_move_t mode) {
    if (!grid_is_floor(cell_x, cell_y)) {
        return;
    }
    int from_x, from_y;
    current_target_cell(&from_x, &from_y);
    if ((cell_x == from_x) && (cell_y == from_y)) {
        return;
    }
    // re-path from the cell Grace is heading to, so she finishes the current
    // edge to a cell centre before turning (keeps movement grid-aligned)
    if (!pathfind_find(from_x, from_y, cell_x, cell_y, &p.path)) {
        return;
    }
    p.speed = speed_for(mode);
    p.path_idx = p.moving ? 0 : 1;
    p.moving = (p.path.len > 1);
    // a fresh order supersedes any pending stop
    p.stop_at_next_cell = false;
}

static void advance_to(const vec2_t center, int cell_x, int cell_y) {
    p.x = center.x;
    p.y = center.y;
    p.cell_x = cell_x;
    p.cell_y = cell_y;
    // one turn for the enemies each time Grace reaches a new cell
    p.steps++;
    p.path_idx++;
    if (p.path_idx >= p.path.len) {
        p.moving = false;
    }
}

// move towards the current waypoint by at most dt of travel. Returns false
// once Grace has arrived (p.moving clears).
static bool step_towards(float dt) {
    const int target = p.path.cells[p.path_idx];
    const int tx = target % GRID_W;
    const int ty = target / GRID_W;
    const vec2_t center = grid_cell_center(tx, ty);
    const float dx = center.x - p.x;
    const float dy = center.y - p.y;
    const float step = p.speed * dt;
    // snap onto the cell centre once it is within one step
    if ((fabsf(dx) <= step) && (fabsf(dy) <= step)) {
        advance_to(center, tx, ty);
        if (p.stop_at_next_cell) {
            p.moving = false;
            p.stop_at_next_cell = false;
        }
        return p.moving;
    }
    // advance along a single axis (the larger remaining delta) so Grace never
    // moves diagonally, even when a new target is picked mid-move
    if (fabsf(dx) >= fabsf(dy)) {
        p.x += (dx > 0.0f) ? step : -step;
    } else {
        p.y += (dy > 0.0f) ? step : -step;
    }
    return true;
}

void player_update(float dt) {
    if (!p.moving || (dt <= 0.0f)) {
        return;
    }
    // Consume the frame in fixed sub-steps so the result does not depend on the
    // frame rate. One step per frame would let a long frame move further than
    // a cell, and the dominant-axis branch would overshoot the waypoint and
    // leave Grace off-grid instead of snapping onto it.
    while (p.moving && (dt > 0.0f)) {
        const float slice = fminf(dt, MAX_SUB_STEP);
        dt -= slice;
        if (!step_towards(slice)) {
            return;
        }
    }
}

vec2_t player_position(void) {
    return (vec2_t){ p.x, p.y };
}

void player_cell(int* cx, int* cy) {
    *cx = p.cell_x;
    *cy = p.cell_y;
}

int player_steps_taken(void) {
    return p.steps;
}

int player_cell_x(void) {
    return p.cell_x;
}

int player_cell_y(void) {
    return p.cell_y;
}

float player_pos_x(void) {
    return p.x;
}

float player_pos_y(void) {
    return p.y;
}