#include "render.h"
#include "camera.h"
#include "enemy.h"
#include "grid.h"
#include "interact.h"
#include "items.h"
#include "level.h"
#include "light.h"
#include "palette.h"
#include "player.h"
#include "stream.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_gl.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

// Reference palette (ref/map.kra)
#define C_INK_R PAL_INK, 1.0f
#define C_GREEN_R PAL_GREEN, 1.0f
#define C_CYAN_R PAL_CYAN, 1.0f
#define C_YELLOW_R PAL_YELLOW, 1.0f
#define C_MAGENTA_R PAL_MAGENTA, 1.0f
#define C_RED_R PAL_RED, 1.0f
#define C_PURPLE_R PAL_PURPLE, 1.0f
#define C_BLUE_R PAL_BLUE, 1.0f
#define C_GRAY_R PAL_GRAY, 1.0f
#define C_DARK_R PAL_INK, 1.0f

static struct {
    sgl_pipeline pip;
    render_fx_t fx;
} r;

void render_set_fx(const render_fx_t* fx) {
    r.fx = *fx;
}

static void fill_rect(float x, float y, float w, float h, float cr, float cg, float cb, float ca) {
    sgl_begin_quads();
    sgl_c4f(cr, cg, cb, ca);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x, y + h);
    sgl_end();
}

static void fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, float cr, float cg, float cb, float ca) {
    sgl_begin_triangles();
    sgl_c4f(cr, cg, cb, ca);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

static void fill_circle(float cx, float cy, float radius, float cr, float cg, float cb, float ca) {
    const int segments = 20;
    sgl_begin_triangle_strip();
    sgl_c4f(cr, cg, cb, ca);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i / (float)segments * 6.2831853f;
        sgl_v2f(cx, cy);
        sgl_v2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
    }
    sgl_end();
}

static void ring_circle(float cx, float cy, float radius, float thickness, float cr, float cg, float cb, float ca) {
    const int segments = 24;
    sgl_begin_triangle_strip();
    sgl_c4f(cr, cg, cb, ca);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i / (float)segments * 6.2831853f;
        sgl_v2f(cx + cosf(a) * (radius - thickness * 0.5f), cy + sinf(a) * (radius - thickness * 0.5f));
        sgl_v2f(cx + cosf(a) * (radius + thickness * 0.5f), cy + sinf(a) * (radius + thickness * 0.5f));
    }
    sgl_end();
}

static void thick_line(float x0, float y0, float x1, float y1, float t, float cr, float cg, float cb, float ca) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) {
        return;
    }
    float nx = -dy / len * t * 0.5f;
    float ny = dx / len * t * 0.5f;
    sgl_begin_quads();
    sgl_c4f(cr, cg, cb, ca);
    sgl_v2f(x0 + nx, y0 + ny);
    sgl_v2f(x1 + nx, y1 + ny);
    sgl_v2f(x1 - nx, y1 - ny);
    sgl_v2f(x0 - nx, y0 - ny);
    sgl_end();
}

// Breathing: her dot eases down to BREATH_MIN of its size and back.
#define BREATH_MIN 0.88f
#define BREATH_WAIT_PERIOD 2.5f    // seconds per breath while she waits
#define BREATH_STRESS_PERIOD 0.9f  // fast, stressed breaths after a hit
static float breath_scale(float clock, float period) {
    float phase = fmodf(clock, period) / period;
    return 1.0f - (1.0f - BREATH_MIN) * (0.5f - 0.5f * cosf(phase * 6.2831853f));
}

// The aim reticle on the enemy: a green ring a bit wider than its dot.
#define RETICLE_RADIUS 0.82f
// Angry enemy: a darker red with a slow pulsing outer ring.
#define ANGRY_PULSE_PERIOD 0.8f

// Damage flashing: on/off a few times a second while time is left.
#define FLASH_PERIOD 0.16f
static bool flash_on(float left) {
    return left > 0.0f && fmodf(left, FLASH_PERIOD) > FLASH_PERIOD * 0.5f;
}

static void outline_cell(float sx, float sy, float scale, float cr, float cg, float cb, float ca) {
    thick_line(sx, sy, sx + scale, sy, 2.0f, cr, cg, cb, ca);
    thick_line(sx + scale, sy, sx + scale, sy + scale, 2.0f, cr, cg, cb, ca);
    thick_line(sx + scale, sy + scale, sx, sy + scale, 2.0f, cr, cg, cb, ca);
    thick_line(sx, sy + scale, sx, sy, 2.0f, cr, cg, cb, ca);
}

// ---- streamed map geometry ------------------------------------------------
// A chunk's cached geometry: the revealed floor cells and the wall edges in
// it, in cell units (screen-size independent, so camera moves never rebuild
// it). Lighting is drawn live per visible floor cell (the lighter moves).
enum { MARK_FLOOR, MARK_VWALL, MARK_HWALL };
typedef struct {
    int16_t x, y;
    uint8_t kind;
} mark_t;
typedef struct {
    int n;
    mark_t m[];
} chunk_geo_t;

static int chunk_marks(int cx, int cy, mark_t* out) {
    int cols = grid_cols(), rows = grid_rows(), n = 0;
    int x0 = cx * STREAM_CHUNK, y0 = cy * STREAM_CHUNK;
    int x1 = x0 + STREAM_CHUNK, y1 = y0 + STREAM_CHUNK;
    if (x1 > cols) x1 = cols;
    if (y1 > rows) y1 = rows;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (grid_is_floor(x, y) && grid_is_revealed(x, y)) {
                if (out) out[n] = (mark_t){ (int16_t)x, (int16_t)y, MARK_FLOOR };
                n++;
            }
        }
    }
    // vertical wall edge at x (between x-1 and x); the map's right border
    // belongs to the last chunk column
    int vx1 = x1 == cols ? cols : x1 - 1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x <= vx1; x++) {
            int ax = x - 1, bx = x;
            bool a = ax >= 0 && grid_is_revealed(ax, y);
            bool b = bx < cols && grid_is_revealed(bx, y);
            if ((a || b) && ((ax < 0 || bx >= cols) ? true : grid_blocked(ax, y, bx, y))) {
                if (out) out[n] = (mark_t){ (int16_t)x, (int16_t)y, MARK_VWALL };
                n++;
            }
        }
    }
    int hy1 = y1 == rows ? rows : y1 - 1;
    for (int y = y0; y <= hy1; y++) {
        for (int x = x0; x < x1; x++) {
            int ay = y - 1, by = y;
            bool a = ay >= 0 && grid_is_revealed(x, ay);
            bool b = by < rows && grid_is_revealed(x, by);
            if ((a || b) && ((ay < 0 || by >= rows) ? true : grid_blocked(x, ay, x, by))) {
                if (out) out[n] = (mark_t){ (int16_t)x, (int16_t)y, MARK_HWALL };
                n++;
            }
        }
    }
    return n;
}

static void chunk_build(int cx, int cy, void** data, int* bytes) {
    int n = chunk_marks(cx, cy, NULL);
    size_t sz = sizeof(chunk_geo_t) + (size_t)n * sizeof(mark_t);
    chunk_geo_t* g = malloc(sz);
    g->n = chunk_marks(cx, cy, g->m);
    *data = g;
    *bytes = (int)sz;
}

static void chunk_free(void* data) {
    free(data);
}

static struct {
    bool overlay;
    bool frozen;             // tests/screenshots: keep the resident set as is
    stream_rect_t frozen_view;
    int cols, rows;
} st;

// The camera view in cells for a centre and the current scale/viewport.
static stream_rect_t view_at(float cx, float cy) {
    float scale = camera_cell_px();
    if (scale <= 0.0f) scale = 16.0f;
    float hw = (float)sapp_width() * 0.5f / scale, hh = (float)sapp_height() * 0.5f / scale;
    return (stream_rect_t){ cx - hw, cy - hh, cx + hw, cy + hh };
}

static stream_rect_t view_now(void) {
    return view_at(camera_center_x(), camera_center_y());
}

static void ensure_stream(void) {
    if (st.cols != grid_cols() || st.rows != grid_rows()) {
        st.cols = grid_cols();
        st.rows = grid_rows();
        stream_init(st.cols, st.rows, chunk_build, chunk_free);
    }
}

void render_map_changed(void) {
    ensure_stream();
    stream_invalidate();
}

void render_stream_prefetch(void) {
    ensure_stream();
    if (!st.frozen) stream_prefetch(view_now());
}

void render_stream_prefetch_at(float cx, float cy) {
    ensure_stream();
    if (!st.frozen) stream_prefetch(view_at(cx, cy));
}

void render_stream_debug(bool overlay, bool freeze) {
    st.overlay = overlay;
    if (freeze && !st.frozen) st.frozen_view = view_now();
    st.frozen = freeze;
}

void render_init(void) {
    r.pip = sgl_make_pipeline(&(sg_pipeline_desc){
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
        .label = "map-pipeline",
    });
}

void render_shutdown(void) {
    sgl_destroy_pipeline(r.pip);
}

static void item_color(int item, float* cr, float* cg, float* cb) {
    item_color_rgb(item, cr, cg, cb);
}

static void door_color(const obj_t* o, float* cr, float* cg, float* cb) {
    door_color_rgb(o->state, cr, cg, cb);
    if (o->state == DOOR_LOCKED && o->lock == LOCK_JAMMED) {
        *cr = *cg = *cb = 0.38f;  // warped or blocked: never opens for Grace
    }
}

// A locked door shows what opens it: a chip in the key's colour, or a
// small bar on the side it opens from.
static void door_lock_mark(const obj_t* o, float lx, float ly, float spanpx, float scale) {
    if (o->state != DOOR_LOCKED || o->open) return;
    float mx = o->horizontal ? lx + spanpx * 0.5f : lx;
    float my = o->horizontal ? ly : ly + spanpx * 0.5f;
    float r = scale * 0.22f;
    if (o->lock == LOCK_KEY && o->key_id > 0) {
        float cr, cg, cb;
        item_color_rgb(o->key_id, &cr, &cg, &cb);
        fill_rect(mx - r, my - r, r * 2.0f, r * 2.0f, C_INK_R);
        fill_rect(mx - r * 0.7f, my - r * 0.7f, r * 1.4f, r * 1.4f, cr, cg, cb, 1.0f);
    } else if (o->lock == LOCK_OTHER_SIDE && o->side != 0) {
        float d = scale * 0.55f * (float)o->side;
        float ox = o->horizontal ? 0.0f : d, oy = o->horizontal ? d : 0.0f;
        fill_rect(mx + ox - r * 0.5f, my + oy - r * 0.5f, r, r, C_INK_R);
    }
}

static void draw_objects_pass(int item_pass) {
    const level_t* lv = grid_level();
    float scale = camera_cell_px();
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if ((o->kind == OBJ_ITEM) != (item_pass != 0)) {
            continue;
        }
        if (!grid_is_revealed(o->x, o->y)) {
            continue;
        }
        {
            // off screen: not drawn (objects span at most a few cells)
            stream_rect_t v = view_now();
            int ow = o->w > 0 ? o->w : (o->kind == OBJ_DOOR && o->horizontal ? o->span : 1);
            int oh = o->h > 0 ? o->h : (o->kind == OBJ_DOOR && !o->horizontal ? o->span : 1);
            if (o->x + ow + 1 < v.x0 || o->x - 1 > v.x1 || o->y + oh + 1 < v.y0 || o->y - 1 > v.y1) {
                continue;
            }
        }
        if (o->kind == OBJ_ITEM && items_taken(i)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && openable_covers_closed(o->x, o->y)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && !light_item_visible(i)) {
            continue;  // in the dark
        }
        if (o->kind == OBJ_DOOR && o->state == DOOR_BROKEN) {
            continue;
        }
        float sx, sy;
        camera_cell_to_screen((float)o->x, (float)o->y, &sx, &sy);
        switch (o->kind) {
            case OBJ_DOOR: {
                float cr, cg, cb;
                door_color(o, &cr, &cg, &cb);
                // pixel-align the door and use whole-pixel widths so the thin
                // borders don't flicker while the follow camera interpolates
                float spanpx = floorf(scale * (o->span > 0 ? o->span : 1) + 0.5f);
                float th = floorf(scale * 0.85f + 0.5f);
                if (th < 2.0f) th = 2.0f;
                if (((int)th & 1) != 0) th += 1.0f;
                float edge = floorf(scale * 0.08f + 0.5f);
                if (edge < 1.0f) edge = 1.0f;
                float lx = floorf(sx + 0.5f);
                float ly = floorf(sy + 0.5f);
                if (o->horizontal) {
                    // on the horizontal wall line at the cell's top edge
                    fill_rect(lx, ly - th * 0.5f, spanpx, th, cr, cg, cb, 1.0f);
                    if (!o->open) {
                        fill_rect(lx, ly - th * 0.5f - edge, spanpx, edge, C_INK_R);
                        fill_rect(lx, ly + th * 0.5f, spanpx, edge, C_INK_R);
                    }
                } else {
                    // on the vertical wall line at the cell's left edge
                    fill_rect(lx - th * 0.5f, ly, th, spanpx, cr, cg, cb, 1.0f);
                    if (!o->open) {
                        fill_rect(lx - th * 0.5f - edge, ly, edge, spanpx, C_INK_R);
                        fill_rect(lx + th * 0.5f, ly, edge, spanpx, C_INK_R);
                    }
                }
                door_lock_mark(o, lx, ly, spanpx, scale);
                break;
            }
            case OBJ_ITEM: {
                float cr, cg, cb;
                item_color(o->item_type, &cr, &cg, &cb);
                float c = sx + scale * 0.5f;
                float d = sy + scale * 0.5f;
                float rad = scale * 0.36f;
                int shape = item_icon_shape(o->item_type);
                if (shape == 1) {
                    fill_rect(c - rad, d - rad, rad * 2.0f, rad * 2.0f, cr, cg, cb, 1.0f);
                } else if (shape == 2) {
                    ring_circle(c, d, rad, scale * 0.16f, cr, cg, cb, 1.0f);
                } else if (shape == 3) {
                    fill_circle(c, d, rad, cr, cg, cb, 1.0f);
                    ring_circle(c, d, rad, scale * 0.1f, C_INK_R);
                } else if (shape == 4) {
                    fill_tri(c, d - rad, c + rad, d + rad * 0.8f, c - rad, d + rad * 0.8f, cr, cg, cb, 1.0f);
                } else if (shape == 5) {
                    fill_tri(c, d - rad, c + rad, d, c - rad, d, cr, cg, cb, 1.0f);
                    fill_tri(c, d + rad, c - rad, d, c + rad, d, cr, cg, cb, 1.0f);
                } else if (shape == 6) {
                    float t = rad * 0.38f;
                    fill_rect(c - rad, d - t, rad * 2.0f, t * 2.0f, cr, cg, cb, 1.0f);
                    fill_rect(c - t, d - rad, t * 2.0f, rad * 2.0f, cr, cg, cb, 1.0f);
                } else if (shape == 7) {
                    float t = scale * 0.12f;
                    fill_rect(c - rad, d - rad, rad * 2.0f, t, cr, cg, cb, 1.0f);
                    fill_rect(c - rad, d + rad - t, rad * 2.0f, t, cr, cg, cb, 1.0f);
                    fill_rect(c - rad, d - rad, t, rad * 2.0f, cr, cg, cb, 1.0f);
                    fill_rect(c + rad - t, d - rad, t, rad * 2.0f, cr, cg, cb, 1.0f);
                } else {
                    fill_circle(c, d, rad, cr, cg, cb, 1.0f);
                }
                break;
            }
            case OBJ_RECIPE: {
                if (o->state) break;
                fill_rect(sx + scale * 0.25f, sy + scale * 0.15f, scale * 0.5f, scale * 0.7f, PAL_PAPER, 1.0f);
                thick_line(sx + scale * 0.32f, sy + scale * 0.35f, sx + scale * 0.68f, sy + scale * 0.35f, scale * 0.06f, C_INK_R);
                thick_line(sx + scale * 0.32f, sy + scale * 0.55f, sx + scale * 0.68f, sy + scale * 0.55f, scale * 0.06f, C_INK_R);
                break;
            }
            case OBJ_CABINET: {
                fill_rect(sx + scale * 0.15f, sy + scale * 0.1f, scale * 0.7f, scale * 0.8f, PAL_GRAY, 1.0f);
                ring_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.18f, scale * 0.07f, C_YELLOW_R);
                break;
            }
            case OBJ_MICROSCOPE: {
                fill_rect(sx + scale * 0.2f, sy + scale * 0.7f, scale * 0.6f, scale * 0.15f, PAL_GRAY, 1.0f);
                thick_line(sx + scale * 0.35f, sy + scale * 0.7f, sx + scale * 0.6f, sy + scale * 0.2f, scale * 0.14f, C_INK_R);
                fill_circle(sx + scale * 0.62f, sy + scale * 0.22f, scale * 0.1f, 0.9f, 0.1f, 0.2f, 1.0f);
                break;
            }
            case OBJ_GOAL: {
                ring_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.36f, scale * 0.1f, C_GREEN_R);
                break;
            }
            case OBJ_LIGHT: {
                float lx = sx + scale * 0.5f;
                float ly = sy + scale * 0.5f;
                if (light_is_on(o)) {
                    fill_circle(lx, ly, scale * 0.60f, 0.949f, 1.0f, 0.0f, 0.18f);
                    fill_circle(lx, ly, scale * 0.40f, 0.949f, 1.0f, 0.0f, 0.35f);
                    fill_circle(lx, ly, scale * 0.30f, 1.0f, 0.95f, 0.2f, 1.0f);
                    ring_circle(lx, ly, scale * 0.33f, scale * 0.06f, C_INK_R);
                } else if (light_is_broken(o)) {
                    fill_circle(lx, ly, scale * 0.30f, 0.35f, 0.33f, 0.30f, 1.0f);
                    float w = scale * 0.10f;
                    fill_rect(lx - scale * 0.26f, ly - scale * 0.20f, w, w, C_INK_R);
                    fill_rect(lx - scale * 0.14f, ly - scale * 0.08f, w, w, C_INK_R);
                    fill_rect(lx - scale * 0.02f, ly + scale * 0.04f, w, w, C_INK_R);
                    fill_rect(lx + scale * 0.10f, ly + scale * 0.16f, w, w, C_INK_R);
                    ring_circle(lx, ly, scale * 0.33f, scale * 0.06f, C_INK_R);
                } else {
                    fill_circle(lx, ly, scale * 0.30f, C_GRAY_R);
                    ring_circle(lx, ly, scale * 0.33f, scale * 0.06f, C_INK_R);
                }
                break;
            }
            case OBJ_SWITCH:
                fill_rect(sx + scale * 0.3f, sy + scale * 0.3f, scale * 0.4f, scale * 0.4f, C_YELLOW_R);
                break;
            case OBJ_TYPEWRITER:
                fill_rect(sx + scale * 0.28f, sy + scale * 0.28f, scale * 0.44f, scale * 0.44f, C_MAGENTA_R);
                break;
            case OBJ_FILE:
                fill_rect(sx + scale * 0.3f, sy + scale * 0.25f, scale * 0.4f, scale * 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
                fill_rect(sx + scale * 0.3f, sy + scale * 0.25f, scale * 0.4f, scale * 0.04f, C_INK_R);
                break;
            case OBJ_SAFE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, C_GRAY_R);
                break;
            case OBJ_OBSTACLE: {
                float w = (float)(o->w > 0 ? o->w : 1) * scale;
                float h = (float)(o->h > 0 ? o->h : 1) * scale;
                float t = scale * 0.12f;
                fill_rect(sx, sy, w, t, C_INK_R);
                fill_rect(sx, sy + h - t, w, t, C_INK_R);
                fill_rect(sx, sy, t, h, C_INK_R);
                fill_rect(sx + w - t, sy, t, h, C_INK_R);
                if (o->openable) {
                    float cr = 0.0f, cg = 1.0f, cb = 0.2f;
                    fill_rect(sx, sy, w, t, cr, cg, cb, 1.0f);
                    fill_rect(sx, sy + h - t, w, t, cr, cg, cb, 1.0f);
                    fill_rect(sx, sy, t, h, cr, cg, cb, 1.0f);
                    fill_rect(sx + w - t, sy, t, h, cr, cg, cb, 1.0f);
                } else if (o->movable) {
                    fill_rect(sx + scale * 0.28f, sy + scale * 0.28f, scale * 0.44f, scale * 0.44f, C_GRAY_R);
                }
                break;
            }
            case OBJ_STAIRS: {
                float w = (float)(o->w > 0 ? o->w : 1) * scale;
                float h = (float)(o->h > 0 ? o->h : 1) * scale;
                int steps = (o->w > o->h ? o->w : o->h) * 2;
                for (int k = 0; k < steps; k += 2) {
                    if (o->w >= o->h) {
                        fill_rect(sx + w * (float)k / (float)steps, sy, w / (float)steps, h, C_GRAY_R);
                    } else {
                        fill_rect(sx, sy + h * (float)k / (float)steps, w, h / (float)steps, C_GRAY_R);
                    }
                }
                break;
            }
            // (movable/openable obstacles are drawn by the OBJ_OBSTACLE case:
            //  green rim for a container, grey block for a movable)
            case OBJ_FUSEBOX:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, C_INK_R);
                fill_rect(sx + scale * 0.35f, sy + scale * 0.35f, scale * 0.3f, scale * 0.3f, C_YELLOW_R);
                break;
            default:
                break;
        }
    }
}

// Non-items first, items on top (so items sit above obstacles/openables).
static void draw_objects(void) {
    draw_objects_pass(0);
    draw_objects_pass(1);
}

static void draw_discovery_markers(void) {
    const level_t* lv = grid_level();
    float scale = camera_cell_px();
    int px, py;
    player_cell(&px, &py);
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_ITEM && o->kind != OBJ_DOOR && o->kind != OBJ_SWITCH &&
            o->kind != OBJ_TYPEWRITER && o->kind != OBJ_FUSEBOX &&
            o->kind != OBJ_FILE && o->kind != OBJ_SAFE && !(o->kind == OBJ_OBSTACLE && o->openable) &&
            !(o->kind == OBJ_OBSTACLE && o->climbable)) {
            continue;
        }
        if (o->kind == OBJ_DOOR && o->state == DOOR_BROKEN) {
            continue;
        }
        if (o->kind == OBJ_ITEM && items_taken(i)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && openable_covers_closed(o->x, o->y)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && !light_item_visible(i)) {
            continue;
        }
        if ((o->kind == OBJ_OBSTACLE && o->openable) && o->open) {
            continue;
        }
        bool rect = (o->kind == OBJ_OBSTACLE);
        int span = o->span > 0 ? o->span : 1;
        int ex = rect ? o->x + (o->w > 0 ? o->w : 1) - 1 : (o->kind == OBJ_DOOR && o->horizontal ? o->x + span - 1 : o->x);
        int ey = rect ? o->y + (o->h > 0 ? o->h : 1) - 1 : (o->kind == OBJ_DOOR && !o->horizontal ? o->y + span - 1 : o->y);
        int nx = px < o->x ? o->x : (px > ex ? ex : px);
        int ny = py < o->y ? o->y : (py > ey ? ey : py);
        if (!grid_is_revealed(nx, ny)) {
            continue;
        }
        int d = interact_distance(i);
        bool door = o->kind == OBJ_DOOR;
        if (d > 4 || (o->kind == OBJ_OBSTACLE && d > 2)) {
            continue;  // climb prompt: outline at 2 cells, X at 1
        }
        // marker position: rect centre, or the midpoint of the door slab
        float mwx = (float)o->x + (rect ? 0.5f * (float)(o->w > 0 ? o->w : 1) : 0.5f);
        float mwy = (float)o->y + (rect ? 0.5f * (float)(o->h > 0 ? o->h : 1) : 0.5f);
        if (o->kind == OBJ_DOOR) {
            if (o->horizontal) {
                mwx = (float)o->x + span * 0.5f;
                mwy = (float)o->y;
            } else {
                mwy = (float)o->y + span * 0.5f;
                mwx = (float)o->x;
            }
        }
        float sx, sy;
        camera_cell_to_screen(mwx, mwy, &sx, &sy);
        float cx = sx;
        float cy = sy;
        // door X appears within two cells; the empty ring still hints out to four
        if ((door && d <= 2) || d <= 1) {
            fill_circle(cx, cy, scale * 0.34f, C_INK_R);
            float e = scale * 0.18f;
            thick_line(cx - e, cy - e, cx + e, cy + e, scale * 0.09f, 1.0f, 1.0f, 1.0f, 1.0f);
            thick_line(cx - e, cy + e, cx + e, cy - e, scale * 0.09f, 1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            ring_circle(cx, cy, scale * 0.34f, scale * 0.07f, C_INK_R);
        }
    }
}

void render_scene(int hover_x, int hover_y, const path_t* path, const path_t* route) {
    int w = sapp_width();
    int h = sapp_height();
    sgl_defaults();
    sgl_viewport(0, 0, w, h, true);
    sgl_load_default_pipeline();
    sgl_load_pipeline(r.pip);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    float scale = camera_cell_px();
    int cols = grid_cols();
    int rows = grid_rows();

    (void)cols;
    (void)rows;
    // Only chunks that intersect the view are drawn, from their cached
    // geometry; they were made resident ahead of time (see stream.h).
    ensure_stream();
    stream_rect_t view = view_now();
    int cx0, cy0, cx1, cy1;
    stream_chunks_in(view, &cx0, &cy0, &cx1, &cy1);
    for (int pass = 0; pass < 2; pass++) {  // floors first, then the wall lines on top
        for (int cy = cy0; cy <= cy1; cy++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                // frozen (debug): show exactly what is loaded, load nothing
                const chunk_geo_t* g = st.frozen ? stream_peek(cx, cy) : stream_acquire(cx, cy);
                if (!g) continue;
                for (int i = 0; i < g->n; i++) {
                    const mark_t* m = &g->m[i];
                    float sx, sy;
                    camera_cell_to_screen((float)m->x, (float)m->y, &sx, &sy);
                    if (pass == 0 && m->kind == MARK_FLOOR) {
                        fill_rect(sx, sy, scale, scale, 1.0f, 1.0f, 1.0f, 1.0f);
                        if (light_cell_lit(m->x, m->y)) {
                            fill_rect(sx, sy, scale, scale, 0.949f, 1.0f, 0.0f, LIGHT_OVERLAY_ALPHA);
                        }
                    } else if (pass == 1 && m->kind == MARK_VWALL) {
                        fill_rect(sx - 1.0f, sy, 2.0f, scale, C_INK_R);
                    } else if (pass == 1 && m->kind == MARK_HWALL) {
                        fill_rect(sx, sy - 1.0f, scale, 2.0f, C_INK_R);
                    }
                }
            }
        }
    }

    if (hover_x >= 0 && hover_y >= 0 && grid_is_floor(hover_x, hover_y)) {
        float sx, sy;
        camera_cell_to_screen((float)hover_x, (float)hover_y, &sx, &sy);
        fill_rect(sx, sy, scale, scale, 0.0f, 1.0f, 0.149f, 0.12f);
        thick_line(sx, sy, sx + scale, sy, 2.0f, C_GREEN_R);
        thick_line(sx + scale, sy, sx + scale, sy + scale, 2.0f, C_GREEN_R);
        thick_line(sx + scale, sy + scale, sx, sy + scale, 2.0f, C_GREEN_R);
        thick_line(sx, sy + scale, sx, sy, 2.0f, C_GREEN_R);
    }

    // doors and items draw on top of the wall lines
    draw_objects();

    if (path && path->count > 1) {
        float px, py;
        camera_cell_to_screen((float)path->x[0] + 0.5f, (float)path->y[0] + 0.5f, &px, &py);
        for (int i = 1; i < path->count; i++) {
            float cx, cy;
            camera_cell_to_screen((float)path->x[i] + 0.5f, (float)path->y[i] + 0.5f, &cx, &cy);
            thick_line(px, py, cx, cy, 2.0f, C_GREEN_R);
            px = cx;
            py = cy;
        }
    }

    if (route && route->count > 1) {
        float px, py;
        camera_cell_to_screen((float)route->x[0] + 0.5f, (float)route->y[0] + 0.5f, &px, &py);
        for (int i = 1; i < route->count; i++) {
            float cx, cy;
            camera_cell_to_screen((float)route->x[i] + 0.5f, (float)route->y[i] + 0.5f, &cx, &cy);
            thick_line(px, py, cx, cy, 2.0f, C_CYAN_R);
            px = cx;
            py = cy;
        }
        int last = route->count - 1;
        float sx, sy;
        camera_cell_to_screen((float)route->x[last], (float)route->y[last], &sx, &sy);
        thick_line(sx, sy, sx + scale, sy, 2.0f, C_CYAN_R);
        thick_line(sx + scale, sy, sx + scale, sy + scale, 2.0f, C_CYAN_R);
        thick_line(sx + scale, sy + scale, sx, sy + scale, 2.0f, C_CYAN_R);
        thick_line(sx, sy + scale, sx, sy, 2.0f, C_CYAN_R);
    }

    draw_discovery_markers();

    if (grid_is_revealed((int)floorf(enemy_x()), (int)floorf(enemy_y())) &&
        !light_cell_lit((int)floorf(enemy_x()), (int)floorf(enemy_y()))) {
        // In the dark the enemy is always just a black outline of its dot:
        // a hollow circle, no fill, whatever Grace can hear or smell. (This
        // replaces the old faint heard-only marker, which would only have
        // duplicated the outline now that it is always drawn.)
        float ex, ey;
        camera_cell_to_screen(enemy_x(), enemy_y(), &ex, &ey);
        ring_circle(ex, ey, scale * 0.64f, scale * 0.09f, C_INK_R);
    } else if (grid_is_revealed((int)floorf(enemy_x()), (int)floorf(enemy_y()))) {
        float ex, ey;
        camera_cell_to_screen(enemy_x(), enemy_y(), &ex, &ey);
        // a winded enemy fades so the escape window reads at a glance
        float alpha = enemy_state() == ENEMY_WINDED ? 0.45f : 1.0f;
        if (flash_on(enemy_flash())) {
            // hit by a bottle: blinks white inside a red ring while stunned
            fill_circle(ex, ey, scale * 0.64f, 1.0f, 1.0f, 1.0f, 1.0f);
            ring_circle(ex, ey, scale * 0.64f, scale * 0.12f, C_RED_R);
        } else if (enemy_angry() > 0.0f) {
            // angry after a hit: darker red, with a pulsing outer ring
            float k = 0.5f - 0.5f * cosf(fmodf(r.fx.clock, ANGRY_PULSE_PERIOD) / ANGRY_PULSE_PERIOD * 6.2831853f);
            fill_circle(ex, ey, scale * 0.64f, 0.6f, 0.0f, 0.08f, alpha);
            ring_circle(ex, ey, scale * (0.74f + 0.1f * k), scale * 0.08f, 0.6f, 0.0f, 0.08f, alpha * (0.9f - 0.6f * k));
        } else {
            fill_circle(ex, ey, scale * 0.64f, 1.0f, 0.0f, 0.165f, alpha);
        }
        if (enemy_state() == ENEMY_BREAKING) {
            ring_circle(ex, ey, scale * 0.64f, scale * 0.12f, C_YELLOW_R);
        }
        if (enemy_state() == ENEMY_STUNNED) {
            ring_circle(ex, ey, scale * 0.64f, scale * 0.12f, C_INK_R);
        }
    }

    {
        float px, py;
        camera_cell_to_screen(player_x(), player_y(), &px, &py);
        if (player_on_top()) {
            ring_circle(px, py, scale * 0.52f, scale * 0.07f, C_INK_R);  // on a table: outlined
        }
        // Waiting: slow breathing; after a hit: the flash, with fast breathing
        // that carries on for a few seconds. Paused: still.
        float gr = scale * 0.51f;
        if (r.fx.grace_stress > 0.0f) {
            gr *= breath_scale(r.fx.clock, BREATH_STRESS_PERIOD);
        } else if (r.fx.grace_waiting) {
            gr *= breath_scale(r.fx.clock, BREATH_WAIT_PERIOD);
        }
        if (flash_on(r.fx.grace_flash)) {
            fill_circle(px, py, gr, 1.0f, 1.0f, 1.0f, 1.0f);
            ring_circle(px, py, gr, scale * 0.12f, C_PURPLE_R);
        } else {
            fill_circle(px, py, gr, C_PURPLE_R);
        }
        // The aim reticle at the end of the straight line (snapped onto the
        // enemy when locked): a ring with cross ticks, never a square.
        if (r.fx.aim) {
            float cx, cy;
            camera_cell_to_screen(r.fx.aim_wx, r.fx.aim_wy, &cx, &cy);
            float rr = scale * RETICLE_RADIUS;
            float t = scale * 0.1f > 2.0f ? scale * 0.1f : 2.0f;
            float cr = r.fx.aim_ok ? 0.0f : 0.616f, cg = r.fx.aim_ok ? 1.0f : 0.616f, cb = r.fx.aim_ok ? 0.149f : 0.616f;
            if (!r.fx.aim_enemy && r.fx.aim_ok) {
                cr = 0.0f; cg = 0.616f; cb = 1.0f;  // cyan line to a floor target, like before
                cr = 0.149f; cg = 0.616f; cb = 1.0f;
            }
            thick_line(px, py, cx, cy, 2.0f, cr, cg, cb, 0.9f);
            ring_circle(cx, cy, rr, t, cr, cg, cb, 1.0f);
            float a = rr + scale * 0.22f, b = rr - scale * 0.22f;
            thick_line(cx - a, cy, cx - b, cy, t, cr, cg, cb, 1.0f);
            thick_line(cx + b, cy, cx + a, cy, t, cr, cg, cb, 1.0f);
            thick_line(cx, cy - a, cx, cy - b, t, cr, cg, cb, 1.0f);
            thick_line(cx, cy + b, cx, cy + a, t, cr, cg, cb, 1.0f);
            fill_circle(cx, cy, t * 0.8f, cr, cg, cb, 1.0f);
        }
    }

    // the bottle's impact: its blue dot, with a ring pulsing out of it
    if (r.fx.impact) {
        float cx, cy;
        camera_cell_to_screen((float)r.fx.impact_x + 0.5f, (float)r.fx.impact_y + 0.5f, &cx, &cy);
        float br, bg, bb;
        item_color(ITEM_BOTTLE, &br, &bg, &bb);
        float a = r.fx.impact_alpha;
        float dot = scale * 0.36f;
        float ring = dot + (scale * 1.4f - dot) * r.fx.impact_pulse;
        fill_circle(cx, cy, dot, br, bg, bb, a);
        ring_circle(cx, cy, ring, scale * 0.1f, br, bg, bb, a * (1.0f - 0.5f * r.fx.impact_pulse));
    }

    // debug overlay: resident chunks green, unloaded grey, the (frozen) view red
    if (st.overlay) {
        for (int cy = 0; cy < stream_chunk_rows(); cy++) {
            for (int cx = 0; cx < stream_chunk_cols(); cx++) {
                float sx, sy;
                camera_cell_to_screen((float)(cx * STREAM_CHUNK), (float)(cy * STREAM_CHUNK), &sx, &sy);
                float sz = scale * STREAM_CHUNK;
                if (stream_is_resident(cx, cy)) {
                    fill_rect(sx, sy, sz, sz, 0.0f, 0.8f, 0.2f, 0.18f);
                }
                thick_line(sx, sy, sx + sz, sy, 1.0f, 0.5f, 0.5f, 0.5f, 0.6f);
                thick_line(sx, sy, sx, sy + sz, 1.0f, 0.5f, 0.5f, 0.5f, 0.6f);
            }
        }
        stream_rect_t v = st.frozen ? st.frozen_view : view;
        float ax, ay, bx, by;
        camera_cell_to_screen(v.x0, v.y0, &ax, &ay);
        camera_cell_to_screen(v.x1, v.y1, &bx, &by);
        thick_line(ax, ay, bx, ay, 3.0f, C_RED_R);
        thick_line(bx, ay, bx, by, 3.0f, C_RED_R);
        thick_line(bx, by, ax, by, 3.0f, C_RED_R);
        thick_line(ax, by, ax, ay, 3.0f, C_RED_R);
    }

    // stream for the next frame: resident = view + LOAD margin
    if (!st.frozen) {
        stream_prefetch(view);
    }
}
