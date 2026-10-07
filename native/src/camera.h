#pragma once
#include <stdbool.h>
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

// ---- panning ----
// When the viewport's shape does not match the map's, the camera scales to
// cover rather than fit, so the map can be larger than the viewport. Pan
// scrolls the map by a screen-space delta (dragging right moves the map right)
// and is clamped, so the viewport never shows space outside the map.
void camera_pan(float dx_px, float dy_px);

// Pan a whole number of cells, used by scroll-wheel and keyboard input.
void camera_pan_cells(float dx_cells, float dy_cells);

// Top-left of the map in screen pixels, so JS can observe the camera and verify
// panning. Exported for the same reason as camera_cell_px.
WEB_EXPORT float camera_origin_x(void);
WEB_EXPORT float camera_origin_y(void);

// True when the map is bigger than the viewport on either axis, i.e. when
// panning can move. False means the whole map is already on screen.
bool camera_pannable(void);
