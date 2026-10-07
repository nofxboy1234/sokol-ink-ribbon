#include "grid.h"

// '#' = wall/void, '.' = walkable floor
static const char* MAP[GRID_H] = {
    "##################",
    "########....######",
    "########....###...",
    "########....###...",
    "########..........",
    "#...####...#####..",
    "#..........#####..",
    "#...############..",
    "#...############..",
    "#...############..",
    "##................",
    "################..",
};

// interior wall edges that are not implied by a void cell
static bool wall_right[GRID_W][GRID_H];
static bool wall_down[GRID_W][GRID_H];

void grid_init(void) {
    // a divider inside the right room (open at its bottom row)
    wall_right[15][2] = true;
    wall_right[15][3] = true;
}

bool grid_is_floor(int x, int y) {
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) {
        return false;
    }
    return MAP[y][x] == '.';
}

bool grid_blocked(int x, int y, int nx, int ny) {
    if (nx == x + 1) { return wall_right[x][y]; }
    if (nx == x - 1) { return wall_right[nx][y]; }
    if (ny == y + 1) { return wall_down[x][y]; }
    if (ny == y - 1) { return wall_down[x][ny]; }
    return false;
}

bool grid_wall_right(int x, int y) {
    return wall_right[x][y];
}

bool grid_wall_down(int x, int y) {
    return wall_down[x][y];
}

vec2_t grid_cell_center(int x, int y) {
    return (vec2_t){ (x + 0.5f) * CELL, (y + 0.5f) * CELL };
}

int grid_cell_index(int x, int y) {
    return y * GRID_W + x;
}
