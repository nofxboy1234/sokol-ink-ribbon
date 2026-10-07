//------------------------------------------------------------------------------
//  main.c
//  Grid map prototype: glowing walls, a player dot and A* movement.
//  Drawn with sokol_gl on top of sokol_gfx.
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"
#include "dbgui/dbgui.h"

#include <math.h>
#include <stdbool.h>

#define GRID_W 18
#define GRID_H 12
#define CELL 40.0f

// '#' = wall/void, '.' = walkable floor
static const char* MAP[GRID_H] = {
    "##################",
    "########....######",
    "########....###...",
    "########....###...",
    "########..........",
    "#...####...#####..",
    "#..........#####..",
    "#...############..",
    "#...############..",
    "#...############..",
    "##................",
    "################..",
};

// interior wall edges that are not implied by a void cell
static bool wall_right[GRID_W][GRID_H];
static bool wall_down[GRID_W][GRID_H];

typedef struct { float x, y; } vec2_t;

static struct {
    sgl_pipeline pip;
    sg_pass_action pass_action;
    float scale, ox, oy;
    float mouse_x, mouse_y;
    int hover_x, hover_y;
    bool ui_captured;
    float px, py;
    int pcell_x, pcell_y;
    int path[GRID_W * GRID_H];
    int path_len;
    int path_idx;
    bool moving;
} state;

static bool is_floor(int x, int y) {
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) {
        return false;
    }
    return MAP[y][x] == '.';
}

static vec2_t cell_center(int x, int y) {
    return (vec2_t){ (x + 0.5f) * CELL, (y + 0.5f) * CELL };
}

static void update_transform(void) {
    const float ww = sapp_widthf();
    const float wh = sapp_heightf();
    const float mw = GRID_W * CELL;
    const float mh = GRID_H * CELL;
    const float margin = 48.0f;
    const float sx = (ww - 2.0f * margin) / mw;
    const float sy = (wh - 2.0f * margin) / mh;
    state.scale = fminf(sx, sy);
    state.ox = (ww - mw * state.scale) * 0.5f;
    state.oy = (wh - mh * state.scale) * 0.5f;
}

static vec2_t to_screen(vec2_t m) {
    return (vec2_t){ state.ox + m.x * state.scale, state.oy + m.y * state.scale };
}

static void quad(vec2_t a, vec2_t b, vec2_t c, vec2_t d) {
    sgl_v2f(a.x, a.y);
    sgl_v2f(b.x, b.y);
    sgl_v2f(c.x, c.y);
    sgl_v2f(d.x, d.y);
}

static void line_seg(vec2_t a, vec2_t b, float width, float r, float g, float bl, float al) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) {
        return;
    }
    const float nx = -dy / len * width * 0.5f;
    const float ny = dx / len * width * 0.5f;
    sgl_c4f(r, g, bl, al);
    quad(
        (vec2_t){ a.x + nx, a.y + ny },
        (vec2_t){ b.x + nx, b.y + ny },
        (vec2_t){ b.x - nx, b.y - ny },
        (vec2_t){ a.x - nx, a.y - ny });
}

// a soft, low-opacity glowing blue line (layered quads fake a bloom)
static void glow_line(vec2_t a, vec2_t b) {
    line_seg(a, b, 20.0f, 0.15f, 0.45f, 1.00f, 0.045f);
    line_seg(a, b, 11.0f, 0.20f, 0.55f, 1.00f, 0.090f);
    line_seg(a, b,  6.0f, 0.28f, 0.65f, 1.00f, 0.180f);
    line_seg(a, b,  3.0f, 0.42f, 0.78f, 1.00f, 0.400f);
    line_seg(a, b,  1.4f, 0.70f, 0.92f, 1.00f, 0.850f);
}

static void glow_dot(vec2_t c) {
    const int N = 48;
    const float PI = 3.14159265f;
    const float radii[5] = { 34.0f, 21.0f, 11.5f, 5.5f, 2.6f };
    const float center_a[5] = { 0.06f, 0.14f, 0.35f, 0.90f, 1.0f };
    const float rim_a[5] = { 0.0f, 0.0f, 0.0f, 0.20f, 0.55f };
    const float cr[5] = { 0.20f, 0.30f, 0.45f, 0.75f, 1.0f };
    const float cg[5] = { 0.50f, 0.62f, 0.78f, 0.92f, 1.0f };
    for (int layer = 0; layer < 5; layer++) {
        const float rad = radii[layer];
        for (int i = 0; i < N; i++) {
            const float a0 = (float)i / (float)N * 2.0f * PI;
            const float a1 = (float)(i + 1) / (float)N * 2.0f * PI;
            sgl_c4f(cr[layer], cg[layer], 1.0f, center_a[layer]);
            sgl_v2f(c.x, c.y);
            sgl_c4f(cr[layer], cg[layer], 1.0f, rim_a[layer]);
            sgl_v2f(c.x + cosf(a0) * rad, c.y + sinf(a0) * rad);
            sgl_v2f(c.x + cosf(a1) * rad, c.y + sinf(a1) * rad);
        }
    }
}

static void cell_border(int cx, int cy) {
    const vec2_t p0 = to_screen((vec2_t){ cx * CELL, cy * CELL });
    const vec2_t p1 = to_screen((vec2_t){ (cx + 1) * CELL, (cy + 1) * CELL });
    const vec2_t a = { p0.x, p0.y };
    const vec2_t b = { p1.x, p0.y };
    const vec2_t c = { p1.x, p1.y };
    const vec2_t d = { p0.x, p1.y };
    line_seg(a, b, 8.0f, 0.20f, 1.00f, 0.35f, 0.10f);
    line_seg(b, c, 8.0f, 0.20f, 1.00f, 0.35f, 0.10f);
    line_seg(c, d, 8.0f, 0.20f, 1.00f, 0.35f, 0.10f);
    line_seg(d, a, 8.0f, 0.20f, 1.00f, 0.35f, 0.10f);
    line_seg(a, b, 2.6f, 0.30f, 1.00f, 0.40f, 0.95f);
    line_seg(b, c, 2.6f, 0.30f, 1.00f, 0.40f, 0.95f);
    line_seg(c, d, 2.6f, 0.30f, 1.00f, 0.40f, 0.95f);
    line_seg(d, a, 2.6f, 0.30f, 1.00f, 0.40f, 0.95f);
}

static void draw_wall_seg(float mx0, float my0, float mx1, float my1) {
    glow_line(
        to_screen((vec2_t){ mx0, my0 }),
        to_screen((vec2_t){ mx1, my1 }));
}

static void draw_walls(void) {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (!is_floor(x, y)) {
                continue;
            }
            if (!is_floor(x - 1, y)) { draw_wall_seg(x * CELL, y * CELL, x * CELL, (y + 1) * CELL); }
            if (!is_floor(x + 1, y)) { draw_wall_seg((x + 1) * CELL, y * CELL, (x + 1) * CELL, (y + 1) * CELL); }
            if (!is_floor(x, y - 1)) { draw_wall_seg(x * CELL, y * CELL, (x + 1) * CELL, y * CELL); }
            if (!is_floor(x, y + 1)) { draw_wall_seg(x * CELL, (y + 1) * CELL, (x + 1) * CELL, (y + 1) * CELL); }
        }
    }
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (wall_right[x][y]) { draw_wall_seg((x + 1) * CELL, y * CELL, (x + 1) * CELL, (y + 1) * CELL); }
            if (wall_down[x][y]) { draw_wall_seg(x * CELL, (y + 1) * CELL, (x + 1) * CELL, (y + 1) * CELL); }
        }
    }
}

static bool blocked(int x, int y, int nx, int ny) {
    if (nx == x + 1) { return wall_right[x][y]; }
    if (nx == x - 1) { return wall_right[nx][y]; }
    if (ny == y + 1) { return wall_down[x][y]; }
    if (ny == y - 1) { return wall_down[x][ny]; }
    return false;
}

static int cell_index(int x, int y) {
    return y * GRID_W + x;
}

static bool find_path(int sx, int sy, int tx, int ty) {
    static float g[GRID_W * GRID_H];
    static float f[GRID_W * GRID_H];
    static int parent[GRID_W * GRID_H];
    static bool open[GRID_W * GRID_H];
    static bool closed[GRID_W * GRID_H];
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        g[i] = 1e9f; f[i] = 1e9f; parent[i] = -1; open[i] = false; closed[i] = false;
    }
    const int start = cell_index(sx, sy);
    const int goal = cell_index(tx, ty);
    g[start] = 0.0f;
    f[start] = (float)(abs(tx - sx) + abs(ty - sy));
    open[start] = true;
    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };
    int current = -1;
    for (;;) {
        current = -1;
        float best = 1e9f;
        for (int i = 0; i < GRID_W * GRID_H; i++) {
            if (open[i] && f[i] < best) { best = f[i]; current = i; }
        }
        if (current < 0) {
            return false;
        }
        if (current == goal) {
            break;
        }
        open[current] = false;
        closed[current] = true;
        const int cx = current % GRID_W;
        const int cy = current / GRID_W;
        for (int k = 0; k < 4; k++) {
            const int nx = cx + dx[k];
            const int ny = cy + dy[k];
            if (!is_floor(nx, ny) || blocked(cx, cy, nx, ny)) {
                continue;
            }
            const int ni = cell_index(nx, ny);
            if (closed[ni]) {
                continue;
            }
            const float ng = g[current] + 1.0f;
            if (ng < g[ni]) {
                g[ni] = ng;
                f[ni] = ng + (float)(abs(tx - nx) + abs(ty - ny));
                parent[ni] = current;
                open[ni] = true;
            }
        }
    }
    state.path_len = 0;
    for (int c = goal; c != -1; c = parent[c]) {
        state.path[state.path_len++] = c;
    }
    for (int i = 0; i < state.path_len / 2; i++) {
        const int tmp = state.path[i];
        state.path[i] = state.path[state.path_len - 1 - i];
        state.path[state.path_len - 1 - i] = tmp;
    }
    state.path_idx = (state.path_len > 1) ? 1 : 0;
    state.moving = (state.path_len > 1);
    return true;
}

static void start_move_to(int cx, int cy) {
    if (cx == state.pcell_x && cy == state.pcell_y) {
        return;
    }
    if (is_floor(cx, cy)) {
        find_path(state.pcell_x, state.pcell_y, cx, cy);
    }
}

static void update_movement(float dt) {
    if (!state.moving) {
        return;
    }
    const int target = state.path[state.path_idx];
    const int tx = target % GRID_W;
    const int ty = target / GRID_W;
    const vec2_t c = cell_center(tx, ty);
    const float dx = c.x - state.px;
    const float dy = c.y - state.py;
    const float dist = sqrtf(dx * dx + dy * dy);
    const float step = CELL * 4.0f * dt;
    if (dist <= step) {
        state.px = c.x;
        state.py = c.y;
        state.pcell_x = tx;
        state.pcell_y = ty;
        state.path_idx++;
        if (state.path_idx >= state.path_len) {
            state.moving = false;
        }
    } else {
        state.px += dx / dist * step;
        state.py += dy / dist * step;
    }
}

static void update_hover(void) {
    if (state.ui_captured) {
        state.hover_x = -1;
        state.hover_y = -1;
        return;
    }
    const float mx = (state.mouse_x - state.ox) / state.scale;
    const float my = (state.mouse_y - state.oy) / state.scale;
    const int cx = (int)floorf(mx / CELL);
    const int cy = (int)floorf(my / CELL);
    if (is_floor(cx, cy)) {
        state.hover_x = cx;
        state.hover_y = cy;
    } else {
        state.hover_x = -1;
        state.hover_y = -1;
    }
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){
        .logger.func = slog_func,
    });
    state.pip = sgl_make_pipeline(&(sg_pipeline_desc){
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
    _dbgui_setup();

    // a divider inside the right room (open at its bottom row)
    wall_right[15][2] = true;
    wall_right[15][3] = true;

    state.pcell_x = 9;
    state.pcell_y = 2;
    const vec2_t c = cell_center(state.pcell_x, state.pcell_y);
    state.px = c.x;
    state.py = c.y;
    state.hover_x = -1;
    state.hover_y = -1;
    state.moving = false;

    state.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f },
        },
    };
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    update_transform();
    update_movement(dt);
    update_hover();

    sgl_defaults();
    sgl_load_pipeline(state.pip);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, sapp_widthf(), sapp_heightf(), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    sgl_begin_quads();
    draw_walls();
    if (state.hover_x >= 0) {
        cell_border(state.hover_x, state.hover_y);
    }
    sgl_end();

    sgl_begin_triangles();
    glow_dot(to_screen((vec2_t){ state.px, state.py }));
    sgl_end();

    _dbgui_update();
    sg_begin_pass(&(sg_pass){
        .action = state.pass_action,
        .swapchain = sglue_swapchain(),
    });
    sgl_draw();
    _dbgui_draw();
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    _dbgui_shutdown();
    sgl_shutdown();
    sg_shutdown();
}

static void event(const sapp_event* e) {
    const bool captured = _dbgui_event_with_retval(e);
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        state.mouse_x = e->mouse_x;
        state.mouse_y = e->mouse_y;
        state.ui_captured = captured;
    } else if (e->type == SAPP_EVENTTYPE_MOUSE_DOWN && e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        state.mouse_x = e->mouse_x;
        state.mouse_y = e->mouse_y;
        state.ui_captured = captured;
        if (!captured) {
            const float mx = (state.mouse_x - state.ox) / state.scale;
            const float my = (state.mouse_y - state.oy) / state.scale;
            start_move_to((int)floorf(mx / CELL), (int)floorf(my / CELL));
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .event_cb = event,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "map.c",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
