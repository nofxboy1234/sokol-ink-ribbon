#pragma once
#include "vec2.h"
#include "webexport.h"

// recompute the map-to-screen transform for the current framebuffer size
void camera_update(void);

vec2_t camera_to_screen(vec2_t map_pos);
vec2_t camera_to_map(vec2_t screen_pos);

// map the given screen position to a grid cell (may be outside the grid)
void camera_cell_at(vec2_t screen_pos, int* cx, int* cy);

// ---- values mirrored by the React inventory panel ----
// (all in CSS pixels, since the web canvas is sized in CSS pixels)

// on-screen size of one map cell in pixels, i.e. CELL * scale, so the
// inventory cells can be drawn exactly the same size as the map cells
WEB_EXPORT float camera_cell_px(void);

// top-left corner of the reserved inventory area in canvas pixels
WEB_EXPORT float camera_inventory_x(void);
WEB_EXPORT float camera_inventory_y(void);
