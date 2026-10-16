#pragma once
#include <stdbool.h>

// Line of sight between cells: walls and closed doors block, open and broken
// doors do not. Shared by the enemy's eyes and Grace's throws.
bool sight_edge_blocked(int ax, int ay, int bx, int by);
bool sight_clear(int x, int y, int tx, int ty);
// The enemy's eyes: as sight_clear, and obstacles or movables in between block too.
bool sight_clear_eyes(int x, int y, int tx, int ty);
bool sight_cell_obstructed(int x, int y);
