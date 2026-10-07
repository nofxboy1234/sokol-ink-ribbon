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

// True when the map is bigger than the viewport on either axis, i.e. when
// panning can move. False means the whole map is already on screen.
bool camera_pannable(void);

// ---- zoom and follow ----
// Back to the default view and tracking Grace again, including the zoom.
void camera_reset(void);

// Re-centre on Grace and resume tracking, but keep the current zoom. Used when
// Grace respawns: throwing the zoom away as well made the view jump hundreds of
// pixels, which reads as the map lurching rather than Grace respawning.
void camera_recentre(void);

// Current zoom, as a multiple of the fitted scale (>= 1). Exported so the
// React shell and tests can read it.
WEB_EXPORT float camera_zoom(void);

// True while the camera is tracking Grace.
WEB_EXPORT bool camera_following(void);

// Resume (or stop) tracking Grace.
void camera_set_follow(bool on);

// Zoom by `factor` about a screen point, so whatever is under it stays under it.
// Zooming out past the fitted scale is ignored.
void camera_zoom_at(vec2_t screen_pos, float factor);

// One wheel notch per unit of `notches`, positive to zoom in. This is the
// cursor-anchored zoom a mouse wheel wants.
void camera_zoom_step(vec2_t screen_pos, float notches);

// Top-left of the map in screen pixels, so JS can observe the camera and verify
// panning. Exported for the same reason as camera_cell_px.
WEB_EXPORT float camera_origin_x(void);
WEB_EXPORT float camera_origin_y(void);
