#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "level.h"

#define GRID_MAX_COLS 256
#define GRID_MAX_ROWS 256

void grid_init(const level_t* lv);
int grid_cols(void);
int grid_rows(void);
const level_t* grid_level(void);

bool grid_in_bounds(int x, int y);
bool grid_is_floor(int x, int y);
bool grid_is_revealed(int x, int y);
bool grid_blocked(int ax, int ay, int bx, int by);

void grid_reveal_around(int x, int y, int radius);
int grid_revealed_count(void);
