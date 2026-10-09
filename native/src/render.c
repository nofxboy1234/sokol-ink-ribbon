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

// Reference palette (ref/map.kra)
#define C_INK_R 0.118f, 0.118f, 0.118f, 1.0f
#define C_GREEN_R 0.0f, 1.0f, 0.149f, 1.0f
#define C_YELLOW_R 0.949f, 1.0f, 0.0f, 1.0f
#define C_MAGENTA_R 1.0f, 0.0f, 0.416f, 1.0f
#define C_RED_R 1.0f, 0.0f, 0.165f, 1.0f
#define C_PURPLE_R 0.588f, 0.118f, 1.0f, 1.0f
#define C_BLUE_R 0.0f, 0.867f, 1.0f, 1.0f
#define C_GRAY_R 0.616f, 0.616f, 0.616f, 1.0f
#define C_DARK_R 0.118f, 0.118f, 0.118f, 1.0f

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

static void item_color(int item, float* cr, float* cg, float* cb) {
    switch (item) {
        case ITEM_BOTTLE: *cr = 0.0f; *cg = 0.867f; *cb = 1.0f; break;
        case ITEM_COIN: *cr = 0.949f; *cg = 1.0f; *cb = 0.0f; break;
        case ITEM_HERB: *cr = 0.0f; *cg = 1.0f; *cb = 0.149f; break;
        case ITEM_INK_RIBBON: *cr = 0.118f; *cg = 0.118f; *cb = 0.118f; break;
        case ITEM_SCREWDRIVER: *cr = 0.616f; *cg = 0.616f; *cb = 0.616f; break;
        case ITEM_INJECTOR: *cr = 1.0f; *cg = 0.0f; *cb = 0.165f; break;
        case ITEM_FUSE: *cr = 0.949f; *cg = 1.0f; *cb = 0.0f; break;
        case ITEM_LIGHTER: *cr = 1.0f; *cg = 0.0f; *cb = 0.416f; break;
        case ITEM_CHERUB_KEY: *cr = 0.588f; *cg = 0.118f; *cb = 1.0f; break;
        default: *cr = 0.118f; *cg = 0.118f; *cb = 0.118f; break;
    }
}

static void door_color(const obj_t* o, float* cr, float* cg, float* cb) {
    switch (o->state) {
        case DOOR_LOCKED: *cr = 1.0f; *cg = 0.0f; *cb = 0.165f; break;
        case DOOR_UNLOCKED: *cr = 0.118f; *cg = 1.0f; *cb = 0.973f; break;
        case DOOR_UNOPENABLE: *cr = 0.118f; *cg = 0.118f; *cb = 0.118f; break;
        default: *cr = 0.616f; *cg = 0.616f; *cb = 0.616f; break;
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
        if (o->kind == OBJ_ITEM && items_taken(i)) {
            continue;
        }
        float sx, sy;
        camera_cell_to_screen((float)o->x, (float)o->y, &sx, &sy);
        switch (o->kind) {
            case OBJ_DOOR: {
                float cr, cg, cb;
                door_color(o, &cr, &cg, &cb);
                float span = scale * (o->span > 0 ? o->span : 1);
                if (o->horizontal) {
                    // on the horizontal wall line at the cell's top edge
                    fill_rect(sx, sy - scale * 0.09f, span, scale * 0.18f, cr, cg, cb, 1.0f);
                    if (!o->open) {
                        fill_rect(sx, sy - scale * 0.12f, span, scale * 0.03f, C_INK_R);
                        fill_rect(sx, sy + scale * 0.09f, span, scale * 0.03f, C_INK_R);
                    }
                } else {
                    // on the vertical wall line at the cell's left edge
                    fill_rect(sx - scale * 0.09f, sy, scale * 0.18f, span, cr, cg, cb, 1.0f);
                    if (!o->open) {
                        fill_rect(sx - scale * 0.12f, sy, scale * 0.03f, span, C_INK_R);
                        fill_rect(sx + scale * 0.09f, sy, scale * 0.03f, span, C_INK_R);
                    }
                }
                break;
            }
            case OBJ_ITEM: {
                float cr, cg, cb;
                item_color(o->item_type, &cr, &cg, &cb);
                fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.22f, cr, cg, cb, 1.0f);
                ring_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.24f, scale * 0.05f, C_INK_R);
                break;
            }
            case OBJ_LIGHT: {
                if (o->state) {
                    fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.14f, C_YELLOW_R);
                } else {
                    fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.14f, C_GRAY_R);
                }
                break;
            }
            case OBJ_SWITCH:
                fill_rect(sx + scale * 0.3f, sy + scale * 0.3f, scale * 0.4f, scale * 0.4f, C_YELLOW_R);
                break;
            case OBJ_TYPEWRITER:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.3f, scale * 0.5f, scale * 0.4f, C_DARK_R);
                break;
            case OBJ_FILE:
                fill_rect(sx + scale * 0.3f, sy + scale * 0.25f, scale * 0.4f, scale * 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
                fill_rect(sx + scale * 0.3f, sy + scale * 0.25f, scale * 0.4f, scale * 0.04f, C_INK_R);
                break;
            case OBJ_SAFE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, C_GRAY_R);
                break;
            case OBJ_OBSTACLE:
                fill_rect(sx + scale * 0.2f, sy + scale * 0.2f, scale * 0.6f, scale * 0.6f, C_GRAY_R);
                break;
            case OBJ_MOVABLE:
                fill_rect(sx + scale * 0.28f, sy + scale * 0.28f, scale * 0.44f, scale * 0.44f, C_GRAY_R);
                break;
            case OBJ_OPENABLE:
                fill_rect(sx + scale * 0.25f, sy + scale * 0.25f, scale * 0.5f, scale * 0.5f, 0.0f, 1.0f, 0.733f, 1.0f);
                break;
            case OBJ_GOAL:
                fill_circle(sx + scale * 0.5f, sy + scale * 0.5f, scale * 0.3f, C_MAGENTA_R);
                break;
            default:
                break;
        }
    }
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
        if (d > 4) {
            continue;
        }
        float sx, sy;
        camera_cell_to_screen((float)o->x, (float)o->y, &sx, &sy);
        float cx = sx + scale * 0.5f;
        float cy = sy + scale * 0.5f;
        if (d <= 1) {
            fill_circle(cx, cy, scale * 0.34f, C_INK_R);
            float e = scale * 0.18f;
            thick_line(cx - e, cy - e, cx + e, cy + e, scale * 0.09f, 1.0f, 1.0f, 1.0f, 1.0f);
            thick_line(cx - e, cy + e, cx + e, cy - e, scale * 0.09f, 1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            ring_circle(cx, cy, scale * 0.34f, scale * 0.07f, C_INK_R);
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
            fill_rect(sx, sy, scale, scale, 1.0f, 1.0f, 1.0f, 1.0f);
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
                fill_rect(sx - 1.0f, sy, 2.0f, scale, C_INK_R);
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
                fill_rect(sx, sy - 1.0f, scale, 2.0f, C_INK_R);
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

    draw_discovery_markers();

    {
        float px, py;
        camera_cell_to_screen(player_x(), player_y(), &px, &py);
        fill_circle(px, py, scale * 0.34f, C_PURPLE_R);
    }
}
