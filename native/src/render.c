#include "render.h"
#include "draw.h"
#include "grid.h"
#include "camera.h"
#include "player.h"
#include "item.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"
#define SOKOL_DEBUGTEXT_IMPL
#include "sokol_debugtext.h"

// pickup popup layout, in screen pixels
#define POPUP_TEXT "Pick up"
#define POPUP_TEXT_LEN 7
#define POPUP_W 128.0f
#define POPUP_H 30.0f
#define POPUP_GAP 18.0f
// sokol_debugtext char cell size on screen (canvas = half the screen)
#define TEXT_CELL 16.0f

static sgl_pipeline pipeline;

static void wall_segment(float mx0, float my0, float mx1, float my1) {
    draw_glow_line(
        camera_to_screen((vec2_t){ mx0, my0 }),
        camera_to_screen((vec2_t){ mx1, my1 }));
}

// walls where a floor cell meets a void cell (or the outside)
static void render_boundary_walls(void) {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (!grid_is_floor(x, y)) {
                continue;
            }
            if (!grid_is_floor(x - 1, y)) { wall_segment(x * CELL, y * CELL, x * CELL, (y + 1) * CELL); }
            if (!grid_is_floor(x + 1, y)) { wall_segment((x + 1) * CELL, y * CELL, (x + 1) * CELL, (y + 1) * CELL); }
            if (!grid_is_floor(x, y - 1)) { wall_segment(x * CELL, y * CELL, (x + 1) * CELL, y * CELL); }
            if (!grid_is_floor(x, y + 1)) { wall_segment(x * CELL, (y + 1) * CELL, (x + 1) * CELL, (y + 1) * CELL); }
        }
    }
}

// explicit interior wall edges
static void render_interior_walls(void) {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (grid_wall_right(x, y)) { wall_segment((x + 1) * CELL, y * CELL, (x + 1) * CELL, (y + 1) * CELL); }
            if (grid_wall_down(x, y)) { wall_segment(x * CELL, (y + 1) * CELL, (x + 1) * CELL, (y + 1) * CELL); }
        }
    }
}

static void draw_rect_frame(vec2_t a, vec2_t b, vec2_t c, vec2_t d, float width, float r, float g, float bl, float al) {
    draw_line(a, b, width, r, g, bl, al);
    draw_line(b, c, width, r, g, bl, al);
    draw_line(c, d, width, r, g, bl, al);
    draw_line(d, a, width, r, g, bl, al);
}

static void render_hover(int cx, int cy) {
    const vec2_t p0 = camera_to_screen((vec2_t){ cx * CELL, cy * CELL });
    const vec2_t p1 = camera_to_screen((vec2_t){ (cx + 1) * CELL, (cy + 1) * CELL });
    const vec2_t a = { p0.x, p0.y };
    const vec2_t b = { p1.x, p0.y };
    const vec2_t c = { p1.x, p1.y };
    const vec2_t d = { p0.x, p1.y };
    draw_rect_frame(a, b, c, d, 8.0f, 0.20f, 1.00f, 0.35f, 0.10f);
    draw_rect_frame(a, b, c, d, 2.6f, 0.30f, 1.00f, 0.40f, 0.95f);
}

static void render_popup_bg(vec2_t min, vec2_t max) {
    const vec2_t a = min;
    const vec2_t b = { max.x, min.y };
    const vec2_t c = max;
    const vec2_t d = { min.x, max.y };
    sgl_c4f(0.02f, 0.05f, 0.03f, 0.88f);
    draw_quad(a, b, c, d);
    draw_rect_frame(a, b, c, d, 1.6f, 0.40f, 1.00f, 0.55f, 0.95f);
}

static void render_items(void) {
    for (int i = 0; i < item_count(); i++) {
        if (!item_present(i)) {
            continue;
        }
        const vec2_t s = camera_to_screen(item_position(i));
        draw_filled_circle(s, 14.0f, 0.20f, 1.00f, 0.40f, 0.12f);
        draw_filled_circle(s, 7.0f, 0.45f, 1.00f, 0.55f, 0.95f);
    }
}

bool render_popup_rect(vec2_t* min, vec2_t* max) {
    const int item = item_any_in_reach();
    // no popup when the inventory is full: there is nothing Grace can do
    if ((item < 0) || !item_can_take(item)) {
        return false;
    }
    const vec2_t hs = camera_to_screen(item_position(item));
    min->x = hs.x - POPUP_W * 0.5f;
    min->y = hs.y - POPUP_GAP - POPUP_H;
    max->x = min->x + POPUP_W;
    max->y = min->y + POPUP_H;
    return true;
}

void render_init(void) {
    sgl_setup(&(sgl_desc_t){
        .logger.func = slog_func,
    });
    pipeline = sgl_make_pipeline(&(sg_pipeline_desc){
        .color_count = 1,
        .colors[0] = {
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .src_factor_alpha = SG_BLENDFACTOR_ONE,
                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            },
        },
        .depth = {
            .write_enabled = false,
            .compare = SG_COMPAREFUNC_ALWAYS,
        },
    });
    sdtx_setup(&(sdtx_desc_t){
        .fonts[0] = sdtx_font_kc853(),
        .logger.func = slog_func,
    });
}

void render_shutdown(void) {
    sdtx_shutdown();
    sgl_shutdown();
}

void render_scene(int hover_x, int hover_y) {
    sgl_defaults();
    sgl_load_pipeline(pipeline);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, sapp_widthf(), sapp_heightf(), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    sgl_begin_quads();
    render_boundary_walls();
    render_interior_walls();
    if (hover_x >= 0) {
        render_hover(hover_x, hover_y);
    }
    vec2_t pmin, pmax;
    if (render_popup_rect(&pmin, &pmax)) {
        render_popup_bg(pmin, pmax);
    }
    sgl_end();

    sgl_begin_triangles();
    draw_glow_dot(camera_to_screen(player_position()));
    render_items();
    sgl_end();
}

void render_draw_overlay(void) {
    sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
    sdtx_origin(0.0f, 0.0f);
    vec2_t pmin, pmax;
    if (render_popup_rect(&pmin, &pmax)) {
        const float tw = POPUP_TEXT_LEN * TEXT_CELL;
        const float tx = pmin.x + (POPUP_W - tw) * 0.5f;
        const float ty = pmin.y + (POPUP_H - TEXT_CELL) * 0.5f;
        sdtx_pos(tx / TEXT_CELL, ty / TEXT_CELL);
        sdtx_color4b(0xd0, 0xff, 0xc8, 0xff);
        sdtx_puts(POPUP_TEXT);
    }
    sdtx_draw();
}