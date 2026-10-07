#pragma once
#include <stdbool.h>
#include "grid.h"

#define PATH_MAX (GRID_W * GRID_H)

typedef struct {
    int cells[PATH_MAX];
    int len;
} path_t;

// 4-directional A* over the floor grid, blocked by interior wall edges.
// On success fills `out` with the cell indices from start to goal.
bool pathfind_find(int sx, int sy, int tx, int ty, path_t* out);
