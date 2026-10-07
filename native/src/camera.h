#pragma once
#include "vec2.h"
#include "webexport.h"

// recompute the map-to-screen transform for the current framebuffer size
void camera_update(void);

vec2_t camera_to_screen(vec2_t map_pos);
vec2_t camera_to_map(vec2_t screen_pos);

// map the given screen position to a grid cell (may be outside the grid)
void camera_cell_at(vec2_t screen_pos, int* cx, int* cy);

// On-screen size of one map cell in pixels, i.e. CELL * scale. The camera fits
// the map to whatever box sokol gives it, so the React shell only needs this
// to size the inventory cells the same as the map cells.
WEB_EXPORT float camera_cell_px(void);
