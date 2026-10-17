#include "sight.h"
#include "doors.h"
#include "grid.h"

#include <stdlib.h>

bool sight_edge_blocked(int ax, int ay, int bx, int by) {
    if (!grid_is_floor(bx, by)) {
        return true;
    }
    return doors_on_edge(ax, ay, bx, by) ? doors_block_sight(ax, ay, bx, by) : grid_blocked(ax, ay, bx, by);
}

// An obstacle or movable object covering the cell (they block eyes, not ears or noses).
bool sight_cell_obstructed(int x, int y) {
    const level_t* lv = grid_level();
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_OBSTACLE && o->kind != OBJ_OBSTACLE) {
            continue;
        }
        int w = o->kind == OBJ_OBSTACLE && o->w > 0 ? o->w : 1;
        int h = o->kind == OBJ_OBSTACLE && o->h > 0 ? o->h : 1;
        if (x >= o->x && x < o->x + w && y >= o->y && y < o->y + h) {
            return true;
        }
    }
    return false;
}

static bool line_clear(int x, int y, int tx, int ty, bool obstacles);

bool sight_clear(int x, int y, int tx, int ty) {
    return line_clear(x, y, tx, ty, false);
}

bool sight_clear_eyes(int x, int y, int tx, int ty) {
    return line_clear(x, y, tx, ty, true);
}

// Bresenham from cell to cell; a diagonal step passes if either corner route is open.
// With obstacles, a cell in between covered by one blocks too.
static bool line_clear(int x, int y, int tx, int ty, bool obstacles) {
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
            bool via_x = !sight_edge_blocked(x, y, nx, y) && !sight_edge_blocked(nx, y, nx, ny);
            bool via_y = !sight_edge_blocked(x, y, x, ny) && !sight_edge_blocked(x, ny, nx, ny);
            if (!via_x && !via_y) {
                return false;
            }
        } else if (sight_edge_blocked(x, y, nx, ny)) {
            return false;
        }
        x = nx;
        y = ny;
        if (obstacles && (x != tx || y != ty) && sight_cell_obstructed(x, y)) {
            return false;
        }
    }
    return true;
}
