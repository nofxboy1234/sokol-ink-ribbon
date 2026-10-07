#include "camera.h"
#include "grid.h"
#include "sokol_app.h"
#include <math.h>

// The map keeps a margin inside whatever box sokol gives it. On a phone a
// fixed margin would eat a quarter of the width, so it shrinks with the frame.
#define MARGIN_MAX 48.0f
#define MARGIN_SCALE 0.05f

static float scale;
static float origin_x;
static float origin_y;

void camera_update(void) {
    const float win_w = sapp_widthf();
    const float win_h = sapp_heightf();
    const float map_w = GRID_W * CELL;
    const float map_h = GRID_H * CELL;
    const float margin = fminf(MARGIN_MAX, fminf(win_w, win_h) * MARGIN_SCALE);
    const float fit_x = (win_w - 2.0f * margin) / map_w;
    const float fit_y = (win_h - 2.0f * margin) / map_h;
    scale = fminf(fit_x, fit_y);
    origin_x = (win_w - map_w * scale) * 0.5f;
    origin_y = (win_h - map_h * scale) * 0.5f;
}

vec2_t camera_to_screen(vec2_t map_pos) {
    return (vec2_t){ origin_x + map_pos.x * scale, origin_y + map_pos.y * scale };
}

vec2_t camera_to_map(vec2_t screen_pos) {
    return (vec2_t){ (screen_pos.x - origin_x) / scale, (screen_pos.y - origin_y) / scale };
}

void camera_cell_at(vec2_t screen_pos, int* cx, int* cy) {
    const vec2_t map_pos = camera_to_map(screen_pos);
    *cx = (int)floorf(map_pos.x / CELL);
    *cy = (int)floorf(map_pos.y / CELL);
}

float camera_cell_px(void) {
    return CELL * scale;
}
