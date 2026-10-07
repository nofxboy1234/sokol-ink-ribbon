#include "render.h"
#include "draw.h"
#include "grid.h"
#include "camera.h"
#include "player.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"

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

static void border_edge(vec2_t a, vec2_t b, vec2_t c, vec2_t d, float width, float alpha) {
    draw_line(a, b, width, 0.20f, 1.00f, 0.35f, alpha);
    draw_line(b, c, width, 0.20f, 1.00f, 0.35f, alpha);
    draw_line(c, d, width, 0.20f, 1.00f, 0.35f, alpha);
    draw_line(d, a, width, 0.20f, 1.00f, 0.35f, alpha);
}

static void render_hover(int cx, int cy) {
    const vec2_t p0 = camera_to_screen((vec2_t){ cx * CELL, cy * CELL });
    const vec2_t p1 = camera_to_screen((vec2_t){ (cx + 1) * CELL, (cy + 1) * CELL });
    const vec2_t a = { p0.x, p0.y };
    const vec2_t b = { p1.x, p0.y };
    const vec2_t c = { p1.x, p1.y };
    const vec2_t d = { p0.x, p1.y };
    border_edge(a, b, c, d, 8.0f, 0.10f);
    border_edge(a, b, c, d, 2.6f, 0.95f);
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
}

void render_shutdown(void) {
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
    sgl_end();

    sgl_begin_triangles();
    draw_glow_dot(camera_to_screen(player_position()));
    sgl_end();
}
