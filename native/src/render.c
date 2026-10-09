#include "render.h"
#include "camera.h"
#include "grid.h"
#include "items.h"
#include "level.h"
#include "player.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_gl.h"

#include <math.h>

#define GLOW_LAYERS 5

static struct {
    sgl_pipeline pip;
} r;

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

static void fill_rect(float x, float y, float w, float h, float cr, float cg, float cb, float ca) {
    sgl_begin_quads();
    sgl_c4f(cr, cg, cb, ca);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x, y + h);
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

static void wall_glow_h(float x0, float y, float x1, float scale) {
    static const float widths[GLOW_LAYERS] = { 20.0f, 11.0f, 6.0f, 3.0f, 1.4f };
    static const float cols[GLOW_LAYERS][4] = {
        { 0.055f, 0.149f, 0.376f, 0.05f },
        { 0.078f, 0.169f, 0.380f, 0.08f },
        { 0.180f, 0.439f, 0.855f, 0.22f },
        { 0.314f, 0.584f, 0.878f, 0.50f },
        { 0.620f, 0.812f, 0.898f, 0.70f },
    };
    (void)scale;
    for (int i = 0; i < GLOW_LAYERS; i++) {
        float h = widths[i];
        fill_rect(x0, y - h * 0.5f, x1 - x0, h, cols[i][0], cols[i][1], cols[i][2], cols[i][3]);
    }
}

static void wall_glow_v(float x, float y0, float y1, float scale) {
    static const float widths[GLOW_LAYERS] = { 20.0f, 11.0f, 6.0f, 3.0f, 1.4f };
    static const float cols[GLOW_LAYERS][4] = {
        { 0.055f, 0.149f, 0.376f, 0.05f },
        { 0.078f, 0.169f, 0.380f, 0.08f },
        { 0.180f, 0.439f, 0.855f, 0.22f },
        { 0.314f, 0.584f, 0.878f, 0.50f },
        { 0.620f, 0.812f, 0.898f, 0.70f },
    };
    (void)scale;
    for (int i = 0; i < GLOW_LAYERS; i++) {
        float w = widths[i];
        fill_rect(x - w * 0.5f, y0, w, y1 - y0, cols[i][0], cols[i][1], cols[i][2], cols[i][3]);
    }
}

static void item_color(int item, float* cr, float* cg, float* cb) {
    switch (item) {
        case ITEM_BOTTLE: *cr = 0.6f; *cg = 0.75f; *cb = 0.85f; break;
        case ITEM_COIN: *cr = 0.95f; *cg = 0.8f; *cb = 0.25f; break;
        case ITEM_HERB: *cr = 0.35f; *cg = 0.85f; *cb = 0.35f; break;
        case ITEM_INK_RIBBON: *cr = 0.15f; *cg = 0.15f; *cb = 0.2f; break;
        case ITEM_SCREWDRIVER: *cr = 0.8f; *cg = 0.8f; *cb = 0.85f; break;
        case ITEM_INJECTOR: *cr = 0.9f; *cg = 0.3f; *cb = 0.3f; break;
        case ITEM_FUSE: *cr = 0.95f; *cg = 0.65f; *cb = 0.2f; break;
        case ITEM_LIGHTER: *cr = 0.98f; *cg = 0.55f; *cb = 0.1f; break;
        case ITEM_CHERUB_KEY: *cr = 0.85f; *cg = 0.7f; *cb = 0.95f; break;
        default: *cr = 0.8f; *cg = 0.8f; *cb = 0.8f; break;
    }
}

static void door_color(const obj_t* o, float* cr, float* cg, float* cb) {
    switch (o->state) {
        case DOOR_LOCKED: *cr = 0.85f; *cg = 0.25f; *cb = 0.25f; break;
        case DOOR_UNLOCKED: *cr = 0.25f; *cg = 0.55f; *cb = 0.95f; break;
        case DOOR_UNOPENABLE: *cr = 0.35f; *cg = 0.35f; *cb = 0.4f; break;
        default: *cr = 0.5f; *cg = 0.5f; *cb = 0.5f; break;
    }
}

static void draw_objects(void) {
    const level_t* lv = grid_level();
    float scale = camera_cell_px();
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (!grid_is_revealed(o->x, o->y)) {
            continue;
        }
        float sx, sy;
        camera_cell_to_screen((float)o->x, (float)o->y, &sx, &sy);
        switch (o->kind) {
            case OBJ_DOOR: {
                float cr, cg, cb;
                door_color(o, &cr, &cg, &cb);
                if (o->horizontal) {
                    fill_rect(sx, sy + scale * 0.4f, scale, scale * 0.2f, cr, cg, cb, 1.0f);
                } else {
                    fill_rect(sx + scale * 0.4f, sy, scale * 0.2f, scale, cr, cg, cb, 1.0f);
                }
                if (!o->open) {
                    if (o->horizontal) {
                        fill_rect(sx, sy + scale * 0.36f, scale, scale * 0.04f, 0.0f, 0.0f, 0.0f, 0.9f);
                        fill_rect(sx, sy + scale * 0.6f, scale, scale * 0.04f, 0.0f, 0.0f, 0.0f, 0.9f);
                    } else {
                        fill_rect(sx + scale * 0.36f, sy, scale * 0.04f, scale, 0.0f, 0.0f, 0.0f, 0.9f);
                        fill_rect(sx + scale * 0.6f, sy, scale * 0.04f, scale, 0.0f, 0.0f, 0.0f, 0.9f);
                    }
                }
                break;
            }
            case OBJ_ITEM: {
                float cr, cg, cb;
                item_color(o->item_type, &cr, &cg, &cb);
                fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.28f, cr, cg, cb, 1.0f);
                break;
            }
            case OBJ_LIGHT: {
                if (o->state) {
                    float radius = (o->radius > 0.0f ? o->radius : 3.0f) * scale;
                    fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, radius, 1.0f, 0.85f, 0.4f, 0.08f);
                }
                fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.12f, 1.0f, 0.9f, 0.5f, 0.9f);
                break;
            }
            case OBJ_SWITCH:
                fill_rect(sx + scale * 0.35f, sy + scale * 0.35f, scale * 0.3f, scale * 0.3f, 0.4f, 0.9f, 0.95f, 1.0f);
                break;
            case OBJ_TYPEWRITER:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, 0.9f, 0.9f, 0.85f, 1.0f);
                break;
            case OBJ_FILE:
                fill_rect(sx + scale * 0.3f, sy + scale * 0.25f, scale * 0.4f, scale * 0.5f, 0.95f, 0.95f, 0.9f, 1.0f);
                break;
            case OBJ_SAFE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, 0.55f, 0.4f, 0.25f, 1.0f);
                break;
            case OBJ_OBSTACLE:
                fill_rect(sx + scale * 0.2f, sy + scale * 0.2f, scale * 0.6f, scale * 0.6f, 0.45f, 0.4f, 0.35f, 1.0f);
                break;
            case OBJ_MOVABLE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, 0.5f, 0.45f, 0.3f, 1.0f);
                break;
            case OBJ_OPENABLE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, 0.6f, 0.5f, 0.7f, 1.0f);
                break;
            case OBJ_GOAL:
                fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.4f, 0.9f, 0.2f, 0.8f, 1.0f);
                break;
            default:
                break;
        }
    }
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

static void draw_discovery_markers(void) {
    const level_t* lv = grid_level();
    float scale = camera_cell_px();
    int px, py;
    player_cell(&px, &py);
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_ITEM && o->kind != OBJ_DOOR) {
            continue;
        }
        if (o->kind == OBJ_ITEM && items_taken(i)) {
            continue;
        }
        if (!grid_is_revealed(o->x, o->y)) {
            continue;
        }
        int dx = o->x - px;
        int dy = o->y - py;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int d = adx > ady ? adx : ady;
        if (d > 2) {
            continue;
        }
        float sx, sy;
        camera_cell_to_screen((float)o->x, (float)o->y, &sx, &sy);
        float cx = sx + scale * 0.5f;
        float cy = sy + scale * 0.5f;
        if (d <= 1) {
            fill_circle(cx, cy, scale * 0.32f, 0.0f, 0.0f, 0.0f, 0.85f);
            float e = scale * 0.2f;
            thick_line(cx - e, cy - e, cx + e, cy + e, scale * 0.08f, 1.0f, 1.0f, 1.0f, 0.95f);
            thick_line(cx - e, cy + e, cx + e, cy - e, scale * 0.08f, 1.0f, 1.0f, 1.0f, 0.95f);
        } else {
            ring_circle(cx, cy, scale * 0.34f, scale * 0.06f, 0.0f, 0.0f, 0.0f, 0.8f);
        }
    }
}

void render_scene(int hover_x, int hover_y, const path_t* path) {
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

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            if (!grid_is_floor(x, y) || !grid_is_revealed(x, y)) {
                continue;
            }
            float sx, sy;
            camera_cell_to_screen((float)x, (float)y, &sx, &sy);
            float shade = ((x + y) & 1) ? 0.0f : 0.012f;
            fill_rect(sx, sy, scale, scale, 0.035f + shade, 0.06f + shade, 0.12f + shade, 1.0f);
        }
    }

    if (hover_x >= 0 && hover_y >= 0 && grid_is_floor(hover_x, hover_y)) {
        float sx, sy;
        camera_cell_to_screen((float)hover_x, (float)hover_y, &sx, &sy);
        fill_rect(sx, sy, scale, scale, 0.3f, 0.95f, 0.5f, 0.18f);
        ring_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.4f, 2.0f, 0.35f, 0.95f, 0.55f, 0.9f);
    }

    draw_objects();
    draw_discovery_markers();

    if (path && path->count > 1) {
        float px, py;
        camera_cell_to_screen((float)path->x[0] + 0.5f, (float)path->y[0] + 0.5f, &px, &py);
        for (int i = 1; i < path->count; i++) {
            float cx, cy;
            camera_cell_to_screen((float)path->x[i] + 0.5f, (float)path->y[i] + 0.5f, &cx, &cy);
            float dx = cx - px;
            float dy = cy - py;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.0f) {
                float nx = -dy / len * 1.5f;
                float ny = dx / len * 1.5f;
                sgl_begin_quads();
                sgl_c4f(0.3f, 0.95f, 0.5f, 0.9f);
                sgl_v2f(px + nx, py + ny);
                sgl_v2f(cx + nx, cy + ny);
                sgl_v2f(cx - nx, cy - ny);
                sgl_v2f(px - nx, py - ny);
                sgl_end();
            }
            px = cx;
            py = cy;
        }
    }

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x <= cols; x++) {
            int ax = x - 1;
            int bx = x;
            bool a = ax >= 0 && grid_is_revealed(ax, y);
            bool b = bx < cols && grid_is_revealed(bx, y);
            if (!(a || b)) {
                continue;
            }
            bool blocked = (ax < 0 || bx >= cols) ? true : grid_blocked(ax, y, bx, y);
            if (blocked) {
                float sx, sy;
                camera_cell_to_screen((float)x, (float)y, &sx, &sy);
                wall_glow_v(sx, sy, sy + scale, scale);
            }
        }
    }
    for (int y = 0; y <= rows; y++) {
        for (int x = 0; x < cols; x++) {
            int ay = y - 1;
            int by = y;
            bool a = ay >= 0 && grid_is_revealed(x, ay);
            bool b = by < rows && grid_is_revealed(x, by);
            if (!(a || b)) {
                continue;
            }
            bool blocked = (ay < 0 || by >= rows) ? true : grid_blocked(x, ay, x, by);
            if (blocked) {
                float sx, sy;
                camera_cell_to_screen((float)x, (float)y, &sx, &sy);
                wall_glow_h(sx, sy, sx + scale, scale);
            }
        }
    }

    {
        float px, py;
        camera_cell_to_screen(player_x(), player_y(), &px, &py);
        fill_circle(px, py, scale * 0.3f, 0.75f, 0.4f, 0.95f, 1.0f);
        fill_circle(px, py, scale * 0.16f, 0.95f, 0.85f, 1.0f, 1.0f);
    }
}
