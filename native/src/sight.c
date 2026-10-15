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

// Bresenham from cell to cell; a diagonal step passes if either corner route is open.
bool sight_clear(int x, int y, int tx, int ty) {
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
    }
    return true;
}
