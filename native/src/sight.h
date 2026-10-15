#pragma once
#include <stdbool.h>

// Line of sight between cells: walls and closed doors block, open and broken
// doors do not. Shared by the enemy's eyes and Grace's throws.
bool sight_edge_blocked(int ax, int ay, int bx, int by);
bool sight_clear(int x, int y, int tx, int ty);
