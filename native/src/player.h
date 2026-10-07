#pragma once
#include "vec2.h"

typedef enum {
    PLAYER_MOVE_WALK,
    PLAYER_MOVE_RUN,
} player_move_t;

void player_init(int cell_x, int cell_y);

// walk to the given cell along an A* path at the walk or run speed.
// turn-based: ignored while the player is still moving, or if the cell is
// unreachable.
void player_move_to(int cell_x, int cell_y, player_move_t mode);

void player_update(float dt);

// current position in map units
vec2_t player_position(void);