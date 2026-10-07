#pragma once
#include "vec2.h"
#include "webexport.h"

typedef enum {
    PLAYER_MOVE_WALK,
    PLAYER_MOVE_RUN,
} player_move_t;

void player_init(int cell_x, int cell_y);

// Send Grace back to her start cell and stop moving. Used when an enemy lands a
// hit.
void player_reset(void);

// Stop as soon as possible without leaving Grace between two cells: she
// finishes the edge she is on and halts on the next cell centre.
void player_stop(void);

// walk to the given cell along an A* path at the walk or run speed.
// a new click while moving re-targets (and may switch between walk and run).
// ignored if the cell is unreachable.
void player_move_to(int cell_x, int cell_y, player_move_t mode);

void player_update(float dt);

// current position in map units
vec2_t player_position(void);

// current cell coordinates
void player_cell(int* cx, int* cy);

// Cell coordinates as plain returns, for callers that only need one of them.
WEB_EXPORT int player_cell_x(void);
WEB_EXPORT int player_cell_y(void);

// Position in map units, which moves between cells while Grace walks.
WEB_EXPORT float player_pos_x(void);
WEB_EXPORT float player_pos_y(void);

// Number of cells Grace has entered since the last reset. The map is turn-based
// and enemies act on this, so they move only while Grace does.
WEB_EXPORT int player_steps_taken(void);