#pragma once
#include "vec2.h"

void player_init(int cell_x, int cell_y);

// walk to the given cell along an A* path. turn-based: ignored while the
// player is still moving, or if the cell is unreachable.
void player_move_to(int cell_x, int cell_y);

void player_update(float dt);

// current position in map units
vec2_t player_position(void);
