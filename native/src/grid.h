#pragma once
#include <stdbool.h>
#include "vec2.h"

#define GRID_W 18
#define GRID_H 12
#define CELL 40.0f

void grid_init(void);

bool grid_is_floor(int x, int y);
bool grid_blocked(int x, int y, int nx, int ny);
bool grid_wall_right(int x, int y);
bool grid_wall_down(int x, int y);

vec2_t grid_cell_center(int x, int y);
int grid_cell_index(int x, int y);
