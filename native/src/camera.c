#include "camera.h"
#include "grid.h"
#include "player.h"
#include "sokol_app.h"
#include <math.h>
#include <stdbool.h>

// The map keeps a margin inside whatever box sokol gives it. On a phone a
// fixed margin would eat a quarter of the width, so it shrinks with the frame.
#define MARGIN_MAX 48.0f
#define MARGIN_SCALE 0.05f

// The map is 3:2 landscape, so fitting it to a viewport of a different shape
// leaves empty bands on one axis. Fitting is kept when those bands are small
// (desktop, where the viewport is close to the map's shape), but past this
// fraction of an axis the map is scaled to cover instead and the overflow is
// panned, which keeps the cells readable and the space used.
//
// This is a measure of the resulting map rather than of the viewport's aspect:
// the shell stacks the inventory under the map on narrow windows, which makes
// the viewport taller than the window is wide even when the window itself is
// not portrait.
#define COVER_VOID 0.15f

// Zoom is a multiple of the fit/cover scale. Zooming in shows less of the map;
// zooming out is capped at 1.0 so the map never shrinks below what the
// viewport already decided.
#define ZOOM_MIN 1.0f
#define ZOOM_MAX 4.0f
#define ZOOM_STEP 1.2f

// how fast the camera catches up with Grace, per frame
#define FOLLOW_EASE 0.12f

static float pan_x;
static float pan_y;
static float zoom = 1.0f;
static bool following = true;
static float origin_x;
static float origin_y;
// pixels per map unit; cell_px below is pixels per CELL, which is CELL * scale
static float scale = 1.0f;
static float cell_px;
static float map_screen_w;
static float map_screen_h;
static float view_w;
static float view_h;

// The scale the map would get with no zoom applied: fitted to the viewport with
// a margin, or covering it when that would leave large empty bands.
static float base_scale(void) {
    const float map_w = GRID_W * CELL;
    const float map_h = GRID_H * CELL;
    const float margin = fminf(MARGIN_MAX, fminf(view_w, view_h) * MARGIN_SCALE);
    const float fit = fminf((view_w - 2.0f * margin) / map_w, (view_h - 2.0f * margin) / map_h);
    const float void_x = 1.0f - (map_w * fit) / view_w;
    const float void_y = 1.0f - (map_h * fit) / view_h;
    if ((void_x > COVER_VOID) || (void_y > COVER_VOID)) {
        // Cover exactly, with no margin: any margin would leave a void strip.
        return fmaxf(view_w / map_w, view_h / map_h);
    }
    return fit;
}

static void apply_scale(void) {
    scale = base_scale() * zoom;
    cell_px = CELL * scale;
    map_screen_w = GRID_W * cell_px;
    map_screen_h = GRID_H * cell_px;
}

// Keep the viewport inside the map: the origin may not go past the far edge, and
// may not leave a gap on the near edge unless the map is smaller than the view,
// in which case it is centred instead.
static void clamp_origin(void) {
    const float max_x = view_w - map_screen_w;
    const float max_y = view_h - map_screen_h;
    if (max_x >= 0.0f) {
        origin_x = (view_w - map_screen_w) * 0.5f;
    } else {
        origin_x = fmaxf(max_x, fminf(0.0f, origin_x));
    }
    if (max_y >= 0.0f) {
        origin_y = (view_h - map_screen_h) * 0.5f;
    } else {
        origin_y = fmaxf(max_y, fminf(0.0f, origin_y));
    }
}

// Centre on Grace, falling back to the nearest map edge when centring would
// show space outside the map.
static void follow_origin(void) {
    origin_x = (view_w - map_screen_w) * 0.5f;
    origin_y = (view_h - map_screen_h) * 0.5f;
    if (map_screen_w > view_w) {
        const float target = view_w * 0.5f - player_position().x * scale;
        origin_x = fminf(0.0f, fmaxf(view_w - map_screen_w, target));
    }
    if (map_screen_h > view_h) {
        const float target = view_h * 0.5f - player_position().y * scale;
        origin_y = fminf(0.0f, fmaxf(view_h - map_screen_h, target));
    }
}

static void store_pan(void) {
    pan_x = origin_x - (view_w - map_screen_w) * 0.5f;
    pan_y = origin_y - (view_h - map_screen_h) * 0.5f;
}

void camera_reset(void) {
    pan_x = 0.0f;
    pan_y = 0.0f;
    zoom = 1.0f;
    following = true;
}

void camera_recentre(void) {
    // Only the manual override is dropped. The pan is deliberately left alone:
    // zeroing it would put the origin exactly at the fitted centre in a single
    // frame, which reads as a lurch. Leaving it lets the follow ease glide the
    // view back to Grace over a few frames.
    following = true;
}

float camera_zoom(void) {
    return zoom;
}

bool camera_following(void) {
    return following;
}

void camera_set_follow(bool on) {
    following = on;
}

// Zoom about a screen point, so whatever is under the cursor or between the
// fingers stays under it. Zooming out past the fit is ignored. Zooming does not
// give up tracking - following is most useful when zoomed in, where the map no
// longer fits. Only dragging by hand takes manual control, in camera_pan().
void camera_zoom_at(vec2_t screen_pos, float factor) {
    const float want = zoom * factor;
    if (want < ZOOM_MIN) {
        return;
    }
    const float next = fminf(want, ZOOM_MAX);
    if (next == zoom) {
        return;
    }
    // the map point under the cursor, before the scale changes
    const vec2_t anchor = camera_to_map(screen_pos);
    zoom = next;
    apply_scale();
    // the origin that keeps that same map point under the cursor
    pan_x = screen_pos.x - anchor.x * scale - (view_w - map_screen_w) * 0.5f;
    pan_y = screen_pos.y - anchor.y * scale - (view_h - map_screen_h) * 0.5f;
}

// One wheel notch towards or away from Grace.
void camera_zoom_step(vec2_t screen_pos, float notches) {
    if (notches == 0.0f) {
        return;
    }
    camera_zoom_at(screen_pos, powf(ZOOM_STEP, notches));
}

void camera_update(void) {
    view_w = sapp_widthf();
    view_h = sapp_heightf();
    apply_scale();

    // Start from the fitted centre and re-apply the pan on top. The pan is
    // deliberately not reset when the scale changes: resizing the window
    // should keep whatever the viewport was looking at, clamped to the new
    // map size.
    origin_x = (view_w - map_screen_w) * 0.5f + pan_x;
    origin_y = (view_h - map_screen_h) * 0.5f + pan_y;
    clamp_origin();

    if (following) {
        const float from_x = origin_x;
        const float from_y = origin_y;
        follow_origin();
        // ease towards Grace so the view glides rather than snaps
        origin_x = from_x + (origin_x - from_x) * FOLLOW_EASE;
        origin_y = from_y + (origin_y - from_y) * FOLLOW_EASE;
        clamp_origin();
    }

    store_pan();
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
    return cell_px;
}

void camera_pan(float dx_px, float dy_px) {
    // dragging by hand takes over from following Grace
    following = false;
    origin_x += dx_px;
    origin_y += dy_px;
    clamp_origin();
    store_pan();
}

void camera_pan_cells(float dx_cells, float dy_cells) {
    camera_pan(-dx_cells * CELL * cell_px, -dy_cells * CELL * cell_px);
}

bool camera_pannable(void) {
    return (map_screen_w > view_w) || (map_screen_h > view_h);
}

float camera_origin_x(void) {
    return origin_x;
}

float camera_origin_y(void) {
    return origin_y;
}
