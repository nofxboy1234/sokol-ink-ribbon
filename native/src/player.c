#include "player.h"
#include "grid.h"
#include "pathfind.h"
#include <math.h>

#define PLAYER_SPEED (CELL * 4.0f)

static struct {
    float x, y;
    int cell_x, cell_y;
    path_t path;
    int path_idx;
    bool moving;
} p;

void player_init(int cell_x, int cell_y) {
    p.cell_x = cell_x;
    p.cell_y = cell_y;
    const vec2_t center = grid_cell_center(cell_x, cell_y);
    p.x = center.x;
    p.y = center.y;
    p.moving = false;
}

void player_move_to(int cell_x, int cell_y) {
    if (cell_x == p.cell_x && cell_y == p.cell_y) {
        return;
    }
    if (!grid_is_floor(cell_x, cell_y)) {
        return;
    }
    if (!pathfind_find(p.cell_x, p.cell_y, cell_x, cell_y, &p.path)) {
        return;
    }
    p.path_idx = (p.path.len > 1) ? 1 : 0;
    p.moving = (p.path.len > 1);
}

static void advance_to(const vec2_t center, int cell_x, int cell_y) {
    p.x = center.x;
    p.y = center.y;
    p.cell_x = cell_x;
    p.cell_y = cell_y;
    p.path_idx++;
    if (p.path_idx >= p.path.len) {
        p.moving = false;
    }
}

void player_update(float dt) {
    if (!p.moving) {
        return;
    }
    const int target = p.path.cells[p.path_idx];
    const int tx = target % GRID_W;
    const int ty = target / GRID_W;
    const vec2_t center = grid_cell_center(tx, ty);
    const float dx = center.x - p.x;
    const float dy = center.y - p.y;
    const float dist = sqrtf(dx * dx + dy * dy);
    const float step = PLAYER_SPEED * dt;
    if (dist <= step) {
        advance_to(center, tx, ty);
    } else {
        p.x += dx / dist * step;
        p.y += dy / dist * step;
    }
}

vec2_t player_position(void) {
    return (vec2_t){ p.x, p.y };
}
