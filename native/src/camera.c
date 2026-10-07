#include "camera.h"
#include "grid.h"
#include "inventory.h"
#include "sokol_app.h"
#include <math.h>

// the map keeps a margin on every side
#define MARGIN 48.0f

#ifdef __EMSCRIPTEN__
// The React app renders Grace's inventory in a strip to the right of the map,
// so the web build reserves that width here and keeps the map clear of it. The
// native window has no inventory panel and fills the frame instead.
#define RESERVE_COLS INVENTORY_COLS
#else
#define RESERVE_COLS 0
#endif

// gap between the map and the reserved inventory area (web build only)
#define INVENTORY_GAP 40.0f
#define GAP_USED (RESERVE_COLS > 0 ? INVENTORY_GAP : 0.0f)

static float scale;
static float origin_x;
static float origin_y;
static float inventory_x;
static float inventory_y;

void camera_update(void) {
    const float win_w = sapp_widthf();
    const float win_h = sapp_heightf();
    const float map_w = GRID_W * CELL;
    const float map_h = GRID_H * CELL;

    // The map plus the reserved strip has to fit the width:
    //   map_w * scale + strip_w <= win_w - 2 * MARGIN
    // with strip_w = RESERVE_COLS * CELL * scale + GAP_USED, so
    //   scale = (win_w - GAP_USED - 2 * MARGIN) / (map_w + RESERVE_COLS * CELL)
    // Substituting it removes the dependency on scale, so no iteration is needed.
    const float fit_x = (win_w - GAP_USED - 2.0f * MARGIN) / (map_w + RESERVE_COLS * CELL);
    const float fit_y = (win_h - 2.0f * MARGIN) / map_h;
    scale = fminf(fit_x, fit_y);

    const float strip_w = RESERVE_COLS * CELL * scale;
    const float map_screen_w = map_w * scale;

    // centre the map in the area left of the strip
    origin_x = (win_w - strip_w - map_screen_w) * 0.5f;
    origin_y = (win_h - map_h * scale) * 0.5f;

    // the React panel sits just past the map, vertically centred
    inventory_x = origin_x + map_screen_w + GAP_USED * 0.5f;
    inventory_y = (win_h - INVENTORY_ROWS * CELL * scale) * 0.5f;
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

float camera_inventory_x(void) {
    return inventory_x;
}

float camera_inventory_y(void) {
    return inventory_y;
}
