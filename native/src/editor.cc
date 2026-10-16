//------------------------------------------------------------------------------
//  editor.cc
//  Dear ImGui level editor: draw walls with a snapping line tool, place and
//  edit map objects, and save/load the level JSON.
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"
#include "imgui.h"
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"

#include "level.h"
#include "light.h"
#include "palette.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TOOL_SELECT = 0,
    TOOL_WALL,
    TOOL_PLACE,
    TOOL_LINK,
};

static struct {
    sg_pass_action pass_action;
    level_t level;
    char path[512];
    int tool;
    int place_kind;
    int place_item;
    int place_horizontal;
    int selected;
    int section_selected;
    int sec_grab;
    float sec_dx, sec_dy;
    float sec_fx, sec_fy;
    int drag_obj;
    int drag_dx, drag_dy;
    int obj_grab;
    float obj_fx, obj_fy;
    int wall_selected;
    int wall_drag;
    int link_src;
    int link_selected;
    bool show_sections;
    bool ref_loaded;
    bool ref_show;
    sg_image ref_img;
    sg_view ref_view;
    sg_sampler ref_smp;
    int ref_w, ref_h;
    char ref_path[512];
    float ref_x, ref_y, ref_scale, ref_alpha;
    bool wall_active;
    float wall_x, wall_y;
    float cam_x, cam_y, scale;
    bool panning;
    float pan_x, pan_y;
    bool dirty;
    char status[640];
} ed;

static int g_argc;
static char** g_argv;

static bool file_exists(const char* p) {
    FILE* f = fopen(p, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static void set_level_path(const char* p) {
    char real[4096];
    const char* use = realpath(p, real) ? real : p;
    size_t n = strlen(use);
    if (n >= sizeof(ed.path)) {
        n = sizeof(ed.path) - 1;
    }
    memcpy(ed.path, use, n);
    ed.path[n] = 0;
}

// Find level_01.json whether the editor is launched from native/ or via
// "fibs run editor" from a build directory.
static void resolve_level_path(void) {
    if (g_argc > 1 && g_argv[1] && g_argv[1][0] && strstr(g_argv[1], ".json")) {
        set_level_path(g_argv[1]);
        return;
    }
    static const char* candidates[] = {
        "src/level_01.json",
        "native/src/level_01.json",
        "../src/level_01.json",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (file_exists(candidates[i])) {
            set_level_path(candidates[i]);
            return;
        }
    }
    if (g_argc > 0 && g_argv[0] && strchr(g_argv[0], '/')) {
        char exe_rel[4096];
        snprintf(exe_rel, sizeof(exe_rel), "%s", g_argv[0]);
        char* slash = strrchr(exe_rel, '/');
        if (slash) {
            *slash = 0;
            char cand[4200];
            snprintf(cand, sizeof(cand), "%s/../../../src/level_01.json", exe_rel);
            if (file_exists(cand)) {
                set_level_path(cand);
                return;
            }
        }
    }
    snprintf(ed.path, sizeof(ed.path), "src/level_01.json");
}

static float view_x(float wx) {
    return (wx - ed.cam_x) * ed.scale + sapp_widthf() * 0.5f;
}

static float view_y(float wy) {
    return (wy - ed.cam_y) * ed.scale + sapp_heightf() * 0.5f;
}

static void screen_to_world(float sx, float sy, float* wx, float* wy) {
    *wx = (sx - sapp_widthf() * 0.5f) / ed.scale + ed.cam_x;
    *wy = (sy - sapp_heightf() * 0.5f) / ed.scale + ed.cam_y;
}

// Fit the whole level into the part of the window the floating panels leave free.
static void fit_view(void) {
    float left = 280.0f;
    float right = sapp_widthf() - 340.0f;
    float top = 44.0f;
    float bottom = sapp_heightf() - 12.0f;
    float fit_x = (right - left) / (ed.level.cols + 2.0f);
    float fit_y = (bottom - top) / (ed.level.rows + 2.0f);
    ed.scale = fit_x < fit_y ? fit_x : fit_y;
    float area_cx = (left + right) * 0.5f;
    float area_cy = (top + bottom) * 0.5f;
    ed.cam_x = ed.level.cols * 0.5f - (area_cx - sapp_widthf() * 0.5f) / ed.scale;
    ed.cam_y = ed.level.rows * 0.5f - (area_cy - sapp_heightf() * 0.5f) / ed.scale;
}

static void fill_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    sgl_begin_quads();
    sgl_c4f(r, g, b, a);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x, y + h);
    sgl_end();
}

static void fill_circle(float cx, float cy, float radius, float r, float g, float b, float a) {
    const int segments = 20;
    sgl_begin_triangle_strip();
    sgl_c4f(r, g, b, a);
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / (float)segments * 6.2831853f;
        sgl_v2f(cx, cy);
        sgl_v2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
    }
    sgl_end();
}

static void draw_line(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) {
        return;
    }
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    sgl_begin_quads();
    sgl_c4f(r, g, b, a);
    sgl_v2f(x0 + nx, y0 + ny);
    sgl_v2f(x1 + nx, y1 + ny);
    sgl_v2f(x1 - nx, y1 - ny);
    sgl_v2f(x0 - nx, y0 - ny);
    sgl_end();
}

static void load_level(void) {
    FILE* f = fopen(ed.path, "rb");
    if (!f) {
        snprintf(ed.status, sizeof(ed.status), "new level (no %s)", ed.path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    static char buf[1 << 20];
    if (size < 0 || size >= (long)sizeof(buf)) {
        fclose(f);
        snprintf(ed.status, sizeof(ed.status), "level too large");
        return;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = 0;
    fclose(f);
    if (level_from_json(&ed.level, buf)) {
        snprintf(ed.status, sizeof(ed.status), "loaded %s (%d objects, %d walls)", ed.path,
                 ed.level.obj_count, ed.level.wall_count);
    } else {
        snprintf(ed.status, sizeof(ed.status), "failed to parse %s", ed.path);
    }
    ed.selected = -1;
    ed.section_selected = -1;
}

static void save_level(void) {
    static char buf[1 << 20];
    int n = level_to_json(&ed.level, buf, (int)sizeof(buf));
    if (n < 0) {
        snprintf(ed.status, sizeof(ed.status), "serialize overflow");
        return;
    }
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ed.path);
    FILE* f = fopen(tmp, "wb");
    if (!f) {
        snprintf(ed.status, sizeof(ed.status), "cannot write %s", ed.path);
        return;
    }
    size_t wrote = fwrite(buf, 1, (size_t)n, f);
    if (fclose(f) != 0 || wrote != (size_t)n) {
        snprintf(ed.status, sizeof(ed.status), "write failed for %s", ed.path);
        return;
    }
    remove(ed.path);
    if (rename(tmp, ed.path) != 0) {
        snprintf(ed.status, sizeof(ed.status), "cannot replace %s", ed.path);
        return;
    }
    ed.dirty = false;
    snprintf(ed.status, sizeof(ed.status), "saved %s (%d bytes)", ed.path, n);
}

// Load a reference image to trace over (drawn on the lowest visible layer).
static void load_reference(void) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(ed.ref_path, &w, &h, &n, 4);
    if (!px) {
        snprintf(ed.status, sizeof(ed.status), "cannot load image %s", ed.ref_path);
        return;
    }
    if (ed.ref_loaded) {
        sg_destroy_view(ed.ref_view);
        sg_destroy_image(ed.ref_img);
    }
    sg_image_desc id = {};
    id.width = w;
    id.height = h;
    id.pixel_format = SG_PIXELFORMAT_RGBA8;
    sg_range range = { px, (size_t)w * h * 4 };
    id.data.mip_levels[0] = range;
    ed.ref_img = sg_make_image(&id);
    if (ed.ref_smp.id == 0) {
        sg_sampler_desc sd = {};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        ed.ref_smp = sg_make_sampler(&sd);
    }
    sg_view_desc vd = {};
    vd.texture.image = ed.ref_img;
    ed.ref_view = sg_make_view(&vd);
    stbi_image_free(px);
    ed.ref_w = w;
    ed.ref_h = h;
    ed.ref_loaded = true;
    ed.ref_show = true;
    snprintf(ed.status, sizeof(ed.status), "loaded reference %s (%dx%d)", ed.ref_path, w, h);
}

static void add_object(int kind, int x, int y) {
    if (ed.level.obj_count >= LEVEL_MAX_OBJECTS) {
        return;
    }
    obj_t* o = &ed.level.objs[ed.level.obj_count++];
    memset(o, 0, sizeof(*o));
    int maxid = 0;
    for (int i = 0; i < ed.level.obj_count; i++) {
        if (ed.level.objs[i].id > maxid) {
            maxid = ed.level.objs[i].id;
        }
    }
    o->id = maxid + 1;
    o->kind = (obj_kind_t)kind;
    o->discover_state = -1;
    o->x = x;
    o->y = y;
    o->w = 1;
    o->h = 1;
    o->item_type = ed.place_item;
    o->radius = LIGHT_DEFAULT_RADIUS;
    o->state = (kind == OBJ_LIGHT) ? 1 : 0;
    if (kind == OBJ_DOOR) {
        o->span = 3;
    }
    if (kind == OBJ_OPENABLE || kind == OBJ_OBSTACLE) {
        o->w = 2;
        o->h = 2;
    }
    snprintf(o->name, LEVEL_MAX_NAME, "%s", obj_kind_name(o->kind));
    ed.selected = ed.level.obj_count - 1;
    ed.dirty = true;
}

static void delete_selected(void) {
    if (ed.selected < 0 || ed.selected >= ed.level.obj_count) {
        return;
    }
    for (int i = ed.selected; i < ed.level.obj_count - 1; i++) {
        ed.level.objs[i] = ed.level.objs[i + 1];
    }
    ed.level.obj_count--;
    ed.selected = -1;
    ed.dirty = true;
}

// Centre of a door's bar: along the span, on the wall line it sits on.
static void door_center(const obj_t* o, float* dcx, float* dcy) {
    float s = (float)(o->span > 0 ? o->span : 1);
    if (o->horizontal) {
        *dcx = (float)o->x + s * 0.5f;
        *dcy = (float)o->y;
    } else {
        *dcx = (float)o->x;
        *dcy = (float)o->y + s * 0.5f;
    }
}

static void pick_object(float wx, float wy) {
    int cx = (int)floorf(wx);
    int cy = (int)floorf(wy);
    ed.selected = -1;
    // items win over the obstacle/openable they sit on
    for (int i = ed.level.obj_count - 1; i >= 0; i--) {
        const obj_t* o = &ed.level.objs[i];
        if (o->kind == OBJ_ITEM && o->x == cx && o->y == cy) {
            ed.selected = i;
            return;
        }
    }
    // exact cell first (topmost), doors included
    for (int i = ed.level.obj_count - 1; i >= 0; i--) {
        const obj_t* o = &ed.level.objs[i];
        if (o->kind == OBJ_DOOR) {
            int span = o->span > 0 ? o->span : 1;
            bool hit = o->horizontal
                ? (cy == o->y && cx >= o->x && cx < o->x + span)
                : (cx == o->x && cy >= o->y && cy < o->y + span);
            if (hit) {
                ed.selected = i;
                return;
            }
        } else if (o->kind == OBJ_OBSTACLE || o->kind == OBJ_OPENABLE) {
            int w = o->w > 0 ? o->w : 1;
            int h = o->h > 0 ? o->h : 1;
            if (cx >= o->x && cx < o->x + w && cy >= o->y && cy < o->y + h) {
                ed.selected = i;
                return;
            }
        } else if (o->x == cx && o->y == cy) {
            ed.selected = i;
            return;
        }
    }
    // fall back to the door bar itself (sits astride the wall line)
    for (int i = ed.level.obj_count - 1; i >= 0; i--) {
        const obj_t* o = &ed.level.objs[i];
        if (o->kind != OBJ_DOOR) {
            continue;
        }
        int span = o->span > 0 ? o->span : 1;
        bool hit = o->horizontal
            ? (wy >= (float)o->y - 0.5f && wy <= (float)o->y + 0.5f &&
               wx >= (float)o->x && wx <= (float)(o->x + span))
            : (wx >= (float)o->x - 0.5f && wx <= (float)o->x + 0.5f &&
               wy >= (float)o->y && wy <= (float)(o->y + span));
        if (hit) {
            ed.selected = i;
            return;
        }
    }
}

static float point_seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax;
    float dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float qx = ax + t * dx;
    float qy = ay + t * dy;
    return sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
}

// Select a wall endpoint (for dragging) or a wall body (for deletion).
static void pick_wall(float wx, float wy) {
    ed.wall_selected = -1;
    ed.wall_drag = -1;
    ed.link_src = 0;
    ed.link_selected = -1;
    float best = 0.6f;
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t* w = &ed.level.walls[i];
        float d0 = sqrtf((w->x0 - wx) * (w->x0 - wx) + (w->y0 - wy) * (w->y0 - wy));
        float d1 = sqrtf((w->x1 - wx) * (w->x1 - wx) + (w->y1 - wy) * (w->y1 - wy));
        if (d0 < best) {
            best = d0;
            ed.wall_selected = i;
            ed.wall_drag = 0;
        }
        if (d1 < best) {
            best = d1;
            ed.wall_selected = i;
            ed.wall_drag = 1;
        }
    }
    if (ed.wall_selected >= 0) {
        return;
    }
    best = 0.45f;
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t* w = &ed.level.walls[i];
        float d = point_seg_dist(wx, wy, (float)w->x0, (float)w->y0, (float)w->x1, (float)w->y1);
        if (d < best) {
            best = d;
            ed.wall_selected = i;
        }
    }
}

// Select a link line (between a linked object and its source).
static void pick_link(float wx, float wy) {
    ed.link_selected = -1;
    float best = 0.5f;
    for (int i = 0; i < ed.level.obj_count; i++) {
        const obj_t* o = &ed.level.objs[i];
        if (o->link_id <= 0) {
            continue;
        }
        for (int j = 0; j < ed.level.obj_count; j++) {
            const obj_t* src = &ed.level.objs[j];
            if (src->id != o->link_id) {
                continue;
            }
            float d = point_seg_dist(wx, wy, (float)src->x + 0.5f, (float)src->y + 0.5f,
                                     (float)o->x + 0.5f, (float)o->y + 0.5f);
            if (d < best) {
                best = d;
                ed.link_selected = i;
            }
            break;
        }
    }
}

static void delete_link(void) {
    if (ed.link_selected < 0 || ed.link_selected >= ed.level.obj_count) {
        return;
    }
    ed.level.objs[ed.link_selected].link_id = 0;
    ed.link_selected = -1;
    ed.dirty = true;
}

static void delete_wall(void) {
    if (ed.wall_selected < 0 || ed.wall_selected >= ed.level.wall_count) {
        return;
    }
    for (int i = ed.wall_selected; i < ed.level.wall_count - 1; i++) {
        ed.level.walls[i] = ed.level.walls[i + 1];
    }
    ed.level.wall_count--;
    ed.wall_selected = -1;
    ed.wall_drag = -1;
    ed.link_src = 0;
    ed.link_selected = -1;
    ed.dirty = true;
}

// 1 = horizontal wall under the cursor, 0 = vertical, -1 = neither.
static int wall_orientation_at(float wx, float wy) {
    int hy = (int)floorf(wy + 0.5f);
    int vx = (int)floorf(wx + 0.5f);
    bool horiz = false, vert = false;
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t* w = &ed.level.walls[i];
        if (w->y0 == w->y1) {
            int x0 = w->x0 < w->x1 ? w->x0 : w->x1;
            int x1 = w->x0 < w->x1 ? w->x1 : w->x0;
            if (w->y0 == hy && wx >= (float)x0 - 0.5f && wx <= (float)x1 + 0.5f) {
                horiz = true;
            }
        } else {
            int y0 = w->y0 < w->y1 ? w->y0 : w->y1;
            int y1 = w->y0 < w->y1 ? w->y1 : w->y0;
            if (w->x0 == vx && wy >= (float)y0 - 0.5f && wy <= (float)y1 + 0.5f) {
                vert = true;
            }
        }
    }
    if (horiz && !vert) {
        return 1;
    }
    if (vert && !horiz) {
        return 0;
    }
    return -1;
}

// Drop zero-length wall segments (drawn or dragged into a point).
static void prune_walls(void) {
    int n = 0;
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t w = ed.level.walls[i];
        if (w.x0 == w.x1 && w.y0 == w.y1) {
            continue;
        }
        ed.level.walls[n++] = w;
    }
    if (n != ed.level.wall_count) {
        ed.level.wall_count = n;
        ed.dirty = true;
    }
}

// Place a door centred on the nearest wall line (or the current orientation).
static void place_door(float wx, float wy) {
    int orient = wall_orientation_at(wx, wy);
    int horiz = orient >= 0 ? orient : ed.place_horizontal;
    int span = 3;
    int px, py;
    if (horiz) {
        py = (int)floorf(wy + 0.5f);
        px = (int)floorf(wx - span * 0.5f + 0.5f);
    } else {
        px = (int)floorf(wx + 0.5f);
        py = (int)floorf(wy - span * 0.5f + 0.5f);
    }
    add_object(OBJ_DOOR, px, py);
    obj_t* o = &ed.level.objs[ed.level.obj_count - 1];
    o->horizontal = horiz;
    o->span = span;
}

static void wall_click(float wx, float wy) {
    int px = (int)floorf(wx + 0.5f);
    int py = (int)floorf(wy + 0.5f);
    if (!ed.wall_active) {
        ed.wall_active = true;
        ed.wall_x = (float)px;
        ed.wall_y = (float)py;
        return;
    }
    int x0 = (int)ed.wall_x;
    int y0 = (int)ed.wall_y;
    if ((x0 == px) == (y0 == py)) {
        ed.wall_x = (float)px;
        ed.wall_y = (float)py;
        return;
    }
    if (ed.level.wall_count < LEVEL_MAX_WALLS) {
        wall_seg_t* w = &ed.level.walls[ed.level.wall_count++];
        w->x0 = x0;
        w->y0 = y0;
        w->x1 = px;
        w->y1 = py;
        ed.dirty = true;
    }
    ed.wall_active = false;
}

static void draw_map(void) {
    sgl_defaults();
    sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
    sgl_load_default_pipeline();
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, sapp_widthf(), sapp_heightf(), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    int cols = ed.level.cols;
    int rows = ed.level.rows;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            float sx = view_x((float)x);
            float sy = view_y((float)y);
            fill_rect(sx, sy, ed.scale, ed.scale, PAL_PAPER, 1.0f);
        }
    }
    if (ed.ref_loaded && ed.ref_show) {
        float x0 = view_x(ed.ref_x);
        float y0 = view_y(ed.ref_y);
        float x1 = view_x(ed.ref_x + (float)ed.ref_w * ed.ref_scale);
        float y1 = view_y(ed.ref_y + (float)ed.ref_h * ed.ref_scale);
        sgl_enable_texture();
        sgl_texture(ed.ref_view, ed.ref_smp);
        sgl_begin_quads();
        sgl_c4f(1.0f, 1.0f, 1.0f, ed.ref_alpha);
        sgl_v2f_t2f(x0, y0, 0.0f, 0.0f);
        sgl_v2f_t2f(x1, y0, 1.0f, 0.0f);
        sgl_v2f_t2f(x1, y1, 1.0f, 1.0f);
        sgl_v2f_t2f(x0, y1, 0.0f, 1.0f);
        sgl_end();
        sgl_disable_texture();
    }
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t* w = &ed.level.walls[i];
        draw_line(view_x((float)w->x0), view_y((float)w->y0), view_x((float)w->x1), view_y((float)w->y1),
                  PAL_WALL_PX + 1.0f, PAL_INK, 1.0f);
    }
    if (ed.wall_selected >= 0 && ed.wall_selected < ed.level.wall_count) {
        wall_seg_t* w = &ed.level.walls[ed.wall_selected];
        float ax = view_x((float)w->x0);
        float ay = view_y((float)w->y0);
        float bx = view_x((float)w->x1);
        float by = view_y((float)w->y1);
        draw_line(ax, ay, bx, by, 4.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        fill_rect(ax - 4, ay - 4, 8, 8, 1, 1, 0, 1);
        fill_rect(bx - 4, by - 4, 8, 8, 1, 1, 0, 1);
    }
    for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < ed.level.obj_count; i++) {
        obj_t* o = &ed.level.objs[i];
        if ((o->kind == OBJ_ITEM) != (pass == 1)) {
            continue;
        }
        float cx = view_x((float)o->x + 0.5f);
        float cy = view_y((float)o->y + 0.5f);
        float scale = ed.scale;
        if (o->kind == OBJ_DOOR) {
            float dcx, dcy;
            door_center(o, &dcx, &dcy);
            cx = view_x(dcx);
            cy = view_y(dcy);
        }
        if (o->kind == OBJ_ITEM) {
            float r, g, b;
            item_color_rgb(o->item_type, &r, &g, &b);
            float rad = scale * 0.36f;
            int shape = item_icon_shape(o->item_type);
            if (shape == 1) {
                fill_rect(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, r, g, b, 1.0f);
            } else if (shape == 2) {
                fill_circle(cx, cy, rad, r, g, b, 1.0f);
                fill_circle(cx, cy, rad * 0.55f, PAL_PAPER, 1.0f);
            } else if (shape == 3) {
                fill_circle(cx, cy, rad, PAL_INK, 1.0f);
                fill_circle(cx, cy, rad * 0.8f, r, g, b, 1.0f);
            } else {
                fill_circle(cx, cy, rad, r, g, b, 1.0f);
            }
        } else if (o->kind == OBJ_DOOR) {
            float r, g, b;
            door_color_rgb(o->state, &r, &g, &b);
            float span = scale * (o->span > 0 ? o->span : 1);
            float th = scale * 0.85f;
            float edge = scale * 0.08f;
            if (edge < 1.0f) {
                edge = 1.0f;
            }
            if (o->horizontal) {
                fill_rect(cx - span * 0.5f, cy - th * 0.5f, span, th, r, g, b, 1.0f);
                if (!o->open) {
                    fill_rect(cx - span * 0.5f, cy - th * 0.5f - edge, span, edge, 0.118f, 0.118f, 0.118f, 1.0f);
                    fill_rect(cx - span * 0.5f, cy + th * 0.5f, span, edge, 0.118f, 0.118f, 0.118f, 1.0f);
                }
            } else {
                fill_rect(cx - th * 0.5f, cy - span * 0.5f, th, span, r, g, b, 1.0f);
                if (!o->open) {
                    fill_rect(cx - th * 0.5f - edge, cy - span * 0.5f, edge, span, 0.118f, 0.118f, 0.118f, 1.0f);
                    fill_rect(cx + th * 0.5f, cy - span * 0.5f, edge, span, 0.118f, 0.118f, 0.118f, 1.0f);
                }
            }
        } else if (o->kind == OBJ_LIGHT) {
            // its light radius, as a plain circle (the game also stops light at walls)
            float lr = (o->radius > 0.0f ? o->radius : LIGHT_DEFAULT_RADIUS) * scale;
            bool picked = ed.selected == i;
            float k = o->state ? 1.0f : 0.45f;
            for (int seg = 0; seg < 64; seg++) {
                float a0 = (float)seg / 64.0f * 6.2831853f, a1 = (float)(seg + 1) / 64.0f * 6.2831853f;
                draw_line(cx + cosf(a0) * lr, cy + sinf(a0) * lr, cx + cosf(a1) * lr, cy + sinf(a1) * lr,
                          picked ? 3.0f : 2.0f, 0.8f * k, 0.84f * k, 0.0f, 1.0f);  // the game's yellow, darkened to read on paper
            }
            if (o->state) {
                fill_circle(cx, cy, scale * 0.55f, 0.949f, 1.0f, 0.0f, 0.18f);
                fill_circle(cx, cy, scale * 0.35f, 0.949f, 1.0f, 0.0f, 0.35f);
                fill_circle(cx, cy, scale * 0.33f, 0.118f, 0.118f, 0.118f, 1.0f);
                fill_circle(cx, cy, scale * 0.28f, 1.0f, 0.95f, 0.2f, 1.0f);
            } else {
                fill_circle(cx, cy, scale * 0.33f, 0.118f, 0.118f, 0.118f, 1.0f);
                fill_circle(cx, cy, scale * 0.28f, 0.616f, 0.616f, 0.616f, 1.0f);
            }
        } else if (o->kind == OBJ_OBSTACLE || o->kind == OBJ_OPENABLE) {
            float rx = view_x((float)o->x);
            float ry = view_y((float)o->y);
            float rw = (float)(o->w > 0 ? o->w : 1) * scale;
            float rh = (float)(o->h > 0 ? o->h : 1) * scale;
            if (o->kind == OBJ_OBSTACLE) {
                float t = scale * 0.12f;
                fill_rect(rx, ry, rw, t, PAL_INK, 1.0f);
                fill_rect(rx, ry + rh - t, rw, t, PAL_INK, 1.0f);
                fill_rect(rx, ry, t, rh, PAL_INK, 1.0f);
                fill_rect(rx + rw - t, ry, t, rh, PAL_INK, 1.0f);
            } else {
                float t = scale * 0.12f;
                fill_rect(rx, ry, rw, t, 0.0f, 1.0f, 0.2f, 1.0f);
                fill_rect(rx, ry + rh - t, rw, t, 0.0f, 1.0f, 0.2f, 1.0f);
                fill_rect(rx, ry, t, rh, 0.0f, 1.0f, 0.2f, 1.0f);
                fill_rect(rx + rw - t, ry, t, rh, 0.0f, 1.0f, 0.2f, 1.0f);
            }
        if (i == ed.selected && o->kind != OBJ_OBSTACLE && o->kind != OBJ_OPENABLE) {
                fill_rect(rx - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx + rw - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx + rw - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
            }
        } else if (o->kind == OBJ_START) {
            float size = scale * 0.6f;
            fill_circle(cx, cy, scale * 0.51f, PAL_PURPLE, 1.0f);  // Grace's dot
        } else {
            float r = 0.6f, g = 0.6f, b = 0.6f;
            switch (o->kind) {
                case OBJ_SWITCH: r = 0.949f; g = 1.0f; b = 0.0f; break;
                case OBJ_TYPEWRITER: r = 1.0f; g = 0.0f; b = 0.416f; break;
                case OBJ_FILE: r = 0.85f; g = 0.85f; b = 0.85f; break;  // white in the game, a shade darker here to show on paper
                case OBJ_SAFE: r = 0.616f; g = 0.616f; b = 0.616f; break;
                case OBJ_OBSTACLE: r = 0.5f; g = 0.45f; b = 0.4f; break;
                case OBJ_MOVABLE: r = 0.616f; g = 0.616f; b = 0.616f; break;
                case OBJ_OPENABLE: r = 0.0f; g = 1.0f; b = 0.733f; break;
                case OBJ_FUSEBOX: r = 0.118f; g = 0.118f; b = 0.118f; break;
                case OBJ_START: r = 0.588f; g = 0.118f; b = 1.0f; break;
                case OBJ_GOAL: r = 0.118f; g = 0.118f; b = 0.118f; break;
                default: break;
            }
            float size = scale * 0.3f;
            fill_rect(cx - size, cy - size, size * 2, size * 2, r, g, b, 1.0f);
        }
        if (i == ed.selected) {
            float size = scale * 0.45f;
            draw_line(cx - size, cy - size, cx + size, cy - size, 2.0f, PAL_CYAN, 1);
            draw_line(cx + size, cy - size, cx + size, cy + size, 2.0f, PAL_CYAN, 1);
            draw_line(cx + size, cy + size, cx - size, cy + size, 2.0f, PAL_CYAN, 1);
            draw_line(cx - size, cy + size, cx - size, cy - size, 2.0f, PAL_CYAN, 1);
        }
    }
    }
    if (ed.link_src > 0) {
        for (int j = 0; j < ed.level.obj_count; j++) {
            if (ed.level.objs[j].id == ed.link_src) {
                float ax = view_x((float)ed.level.objs[j].x + 0.5f);
                float ay = view_y((float)ed.level.objs[j].y + 0.5f);
                fill_circle(ax, ay, ed.scale * 0.5f, 1.0f, 0.0f, 1.0f, 0.6f);
            }
        }
    }
    for (int i = 0; i < ed.level.obj_count; i++) {
        obj_t* o = &ed.level.objs[i];
        if (o->link_id <= 0) {
            continue;
        }
        for (int j = 0; j < ed.level.obj_count; j++) {
            if (ed.level.objs[j].id == o->link_id) {
                if (i == ed.link_selected) {
                    draw_line(view_x((float)ed.level.objs[j].x + 0.5f), view_y((float)ed.level.objs[j].y + 0.5f),
                              view_x((float)o->x + 0.5f), view_y((float)o->y + 0.5f), 4.0f, 1.0f, 0.5f, 1.0f, 1.0f);
                } else {
                    draw_line(view_x((float)ed.level.objs[j].x + 0.5f), view_y((float)ed.level.objs[j].y + 0.5f),
                              view_x((float)o->x + 0.5f), view_y((float)o->y + 0.5f), 2.0f, 1.0f, 0.0f, 1.0f, 0.95f);
                }
                break;
            }
        }
    }
    // sections draw over everything (orange outlines only)
    if (ed.show_sections) {
        for (int i = 0; i < ed.level.section_count; i++) {
            section_t* s = &ed.level.sections[i];
            float rx = view_x((float)s->x);
            float ry = view_y((float)s->y);
            float rw = s->w * ed.scale;
            float rh = s->h * ed.scale;
            if (i == ed.section_selected) {
                draw_line(rx, ry, rx + rw, ry, 2.5f, 1.0f, 0.75f, 0.1f, 1);
                draw_line(rx + rw, ry, rx + rw, ry + rh, 2.5f, 1.0f, 0.75f, 0.1f, 1);
                draw_line(rx + rw, ry + rh, rx, ry + rh, 2.5f, 1.0f, 0.75f, 0.1f, 1);
                draw_line(rx, ry + rh, rx, ry, 2.5f, 1.0f, 0.75f, 0.1f, 1);
                fill_rect(rx - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1);
                fill_rect(rx + rw - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1);
                fill_rect(rx - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1);
                fill_rect(rx + rw - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1);
            } else {
                draw_line(rx, ry, rx + rw, ry, 1.5f, 1.0f, 0.55f, 0.0f, 0.95f);
                draw_line(rx + rw, ry, rx + rw, ry + rh, 1.5f, 1.0f, 0.55f, 0.0f, 0.95f);
                draw_line(rx + rw, ry + rh, rx, ry + rh, 1.5f, 1.0f, 0.55f, 0.0f, 0.95f);
                draw_line(rx, ry + rh, rx, ry, 1.5f, 1.0f, 0.55f, 0.0f, 0.95f);
            }
        }
    }
    if (ed.wall_active) {
        float sx = view_x(ed.wall_x);
        float sy = view_y(ed.wall_y);
        fill_rect(sx - 4, sy - 4, 8, 8, 1, 1, 0, 1);
    }
}

static void new_level(void) {
    int cols = ed.level.cols > 0 ? ed.level.cols : 99;
    int rows = ed.level.rows > 0 ? ed.level.rows : 86;
    level_init(&ed.level);
    ed.level.cols = cols;
    ed.level.rows = rows;
    ed.level.start_x = cols / 2;
    ed.level.start_y = rows / 2;
    ed.selected = -1;
    ed.section_selected = -1;
    ed.sec_grab = 0;
    ed.wall_active = false;
    ed.drag_obj = -1;
    ed.drag_dx = 0;
    ed.drag_dy = 0;
    ed.wall_selected = -1;
    ed.wall_drag = -1;
    ed.link_src = 0;
    ed.link_selected = -1;
    ed.show_sections = true;
    ed.dirty = true;
    snprintf(ed.status, sizeof(ed.status), "new empty level (%dx%d)", cols, rows);
}

static void add_section(void) {
    if (ed.level.section_count >= LEVEL_MAX_SECTIONS) {
        return;
    }
    section_t* s = &ed.level.sections[ed.level.section_count];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, LEVEL_MAX_NAME, "section %02d", ed.level.section_count + 1);
    s->x = 0;
    s->y = 0;
    s->w = 10;
    s->h = 10;
    ed.section_selected = ed.level.section_count;
    ed.level.section_count++;
    ed.dirty = true;
}

static void delete_section(void) {
    if (ed.section_selected < 0 || ed.section_selected >= ed.level.section_count) {
        return;
    }
    for (int i = ed.section_selected; i < ed.level.section_count - 1; i++) {
        ed.level.sections[i] = ed.level.sections[i + 1];
    }
    ed.level.section_count--;
    ed.section_selected = -1;
    ed.dirty = true;
}

static void ui_toolbar(void) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools");
    if (ImGui::Button("Save")) {
        save_level();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        load_level();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        new_level();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        fit_view();
    }
    ImGui::Separator();
    ImGui::Text("Tool");
    ImGui::RadioButton("Select", &ed.tool, TOOL_SELECT);
    ImGui::RadioButton("Wall line", &ed.tool, TOOL_WALL);
    ImGui::RadioButton("Place", &ed.tool, TOOL_PLACE);
    ImGui::RadioButton("Link", &ed.tool, TOOL_LINK);
    if (ImGui::Button("Light")) {
        ed.tool = TOOL_PLACE;
        ed.place_kind = OBJ_LIGHT;
    }
    ImGui::SameLine();
    if (ImGui::Button("Switch")) {
        ed.tool = TOOL_PLACE;
        ed.place_kind = OBJ_SWITCH;
    }
    if (ed.tool == TOOL_PLACE) {
        const char* kinds[OBJ_KIND_COUNT];
        for (int i = 0; i < OBJ_KIND_COUNT; i++) {
            kinds[i] = obj_kind_name((obj_kind_t)i);
        }
        ImGui::Combo("Kind", &ed.place_kind, kinds, OBJ_KIND_COUNT);
        if (ed.place_kind == OBJ_ITEM) {
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) {
                items[i] = item_name((item_t)i);
            }
            ImGui::Combo("Item", &ed.place_item, items, ITEM_COUNT);
        }
    }
    ImGui::Separator();
    ImGui::Text("Sections");
    ImGui::SameLine();
    ImGui::Checkbox("show", &ed.show_sections);
    if (ImGui::Button("Add")) {
        add_section();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        delete_section();
    }
    for (int i = 0; i < ed.level.section_count; i++) {
        if (ImGui::Selectable(ed.level.sections[i].name, i == ed.section_selected)) {
            ed.section_selected = i;
        }
    }
    ImGui::Separator();
    ImGui::Text("Reference image");
    ImGui::InputText("path", ed.ref_path, sizeof(ed.ref_path));
    if (ImGui::Button("Load")) {
        load_reference();
    }
    ImGui::SameLine();
    ImGui::Checkbox("show##ref", &ed.ref_show);
    if (ed.ref_loaded) {
        ImGui::DragFloat("ref x", &ed.ref_x, 0.25f);
        ImGui::DragFloat("ref y", &ed.ref_y, 0.25f);
        ImGui::DragFloat("ref scale", &ed.ref_scale, 0.001f, 0.001f, 4.0f, "%.4f");
        ImGui::SliderFloat("opacity", &ed.ref_alpha, 0.0f, 1.0f);
    }
    ImGui::End();
}

static void ui_properties(void) {
    ImGui::SetNextWindowPos(ImVec2(sapp_widthf() - 330, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Properties");
    if (ed.selected >= 0 && ed.selected < ed.level.obj_count) {
        obj_t* o = &ed.level.objs[ed.selected];
        ImGui::Text("Object #%d (%s)", o->id, obj_kind_name(o->kind));
        bool changed = false;
        changed |= ImGui::InputInt("x", &o->x);
        changed |= ImGui::InputInt("y", &o->y);
        changed |= ImGui::InputText("name", o->name, LEVEL_MAX_NAME);
        if (o->kind == OBJ_DOOR) {
            changed |= ImGui::Checkbox("horizontal", (bool*)&o->horizontal);
            const char* states[3] = { "unknown", "locked", "unlocked" };
            changed |= ImGui::Combo("state", &o->state, states, 3);
            int discover = o->discover_state < 0 ? 0 : o->discover_state;
            const char* discovers[3] = { "auto", "locked", "unlocked" };
            if (ImGui::Combo("discovers", &discover, discovers, 3)) {
                o->discover_state = discover == 0 ? -1 : discover;
                changed = true;
            }
            changed |= ImGui::Checkbox("open", (bool*)&o->open);
            changed |= ImGui::InputInt("key_id", &o->key_id);
            changed |= ImGui::InputInt("span", &o->span);
            changed |= ImGui::InputFloat("auto_close", &o->auto_close);
            changed |= ImGui::Checkbox("breakable", (bool*)&o->breakable);
        } else if (o->kind == OBJ_ITEM) {
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) {
                items[i] = item_name((item_t)i);
            }
            changed |= ImGui::Combo("item", &o->item_type, items, ITEM_COUNT);
        } else if (o->kind == OBJ_LIGHT) {
            changed |= ImGui::SliderFloat("light radius", &o->radius, LIGHT_RADIUS_MIN, LIGHT_RADIUS_MAX, "%.1f cells");
            ImGui::TextDisabled("game: unlit items are hidden");
            ImGui::TextDisabled("link: the switch that toggles it");
            changed |= ImGui::Checkbox("on", (bool*)&o->state);
            changed |= ImGui::InputInt("group_id", &o->group_id);
            changed |= ImGui::Checkbox("breakable", (bool*)&o->breakable);
        } else if (o->kind == OBJ_SWITCH) {
            changed |= ImGui::InputInt("group_id", &o->group_id);
        } else if (o->kind == OBJ_OBSTACLE) {
            changed |= ImGui::InputInt("w", &o->w);
            changed |= ImGui::InputInt("h", &o->h);
            changed |= ImGui::Checkbox("climbable", (bool*)&o->climbable);
        } else if (o->kind == OBJ_OPENABLE) {
            changed |= ImGui::InputInt("w", &o->w);
            changed |= ImGui::InputInt("h", &o->h);
            changed |= ImGui::Checkbox("open", (bool*)&o->open);
        } else if (o->kind == OBJ_FILE || o->kind == OBJ_SAFE) {
            changed |= ImGui::InputInt("code", &o->code);
        }
        changed |= ImGui::InputInt("link", &o->link_id);
        ed.dirty |= changed;
        if (ImGui::Button("Delete")) {
            delete_selected();
        }
    } else if (ed.wall_selected >= 0 && ed.wall_selected < ed.level.wall_count) {
        wall_seg_t* w = &ed.level.walls[ed.wall_selected];
        ImGui::Text("Wall #%d", ed.wall_selected);
        bool changed = false;
        changed |= ImGui::InputInt("x0", &w->x0);
        changed |= ImGui::InputInt("y0", &w->y0);
        changed |= ImGui::InputInt("x1", &w->x1);
        changed |= ImGui::InputInt("y1", &w->y1);
        ed.dirty |= changed;
        if (ImGui::Button("Delete")) {
            delete_wall();
        }
    } else if (ed.section_selected >= 0 && ed.section_selected < ed.level.section_count) {
        section_t* s = &ed.level.sections[ed.section_selected];
        ImGui::Text("Section %s", s->name);
        bool changed = false;
        changed |= ImGui::InputText("name", s->name, LEVEL_MAX_NAME);
        changed |= ImGui::InputInt("x", &s->x);
        changed |= ImGui::InputInt("y", &s->y);
        changed |= ImGui::InputInt("w", &s->w);
        changed |= ImGui::InputInt("h", &s->h);
        ed.dirty |= changed;
    } else {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::Text("Level");
        bool changed = false;
        changed |= ImGui::InputText("name", ed.level.name, LEVEL_MAX_NAME);
        ImGui::Text("Start");
        changed |= ImGui::InputInt("start x", &ed.level.start_x);
        changed |= ImGui::InputInt("start y", &ed.level.start_y);
        changed |= ImGui::InputInt("cols", &ed.level.cols);
        changed |= ImGui::InputInt("rows", &ed.level.rows);
        ed.dirty |= changed;
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", ed.status);
    ImGui::Text("zoom: %.1f px/cell%s", ed.scale, ed.dirty ? "  [modified]" : "");
    ImGui::End();
}

static void handle_input(void) {
    ImGuiIO& io = ImGui::GetIO();
    float wx, wy;
    screen_to_world(io.MousePos.x, io.MousePos.y, &wx, &wy);
    if (!io.WantCaptureMouse) {
        if (io.MouseWheel != 0.0f) {
            float before_x = wx;
            float before_y = wy;
            ed.scale *= powf(1.1f, io.MouseWheel);
            if (ed.scale < 4.0f) {
                ed.scale = 4.0f;
            }
            if (ed.scale > 80.0f) {
                ed.scale = 80.0f;
            }
            screen_to_world(io.MousePos.x, io.MousePos.y, &wx, &wy);
            ed.cam_x += before_x - wx;
            ed.cam_y += before_y - wy;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 d = io.MouseDelta;
            ed.cam_x -= d.x / ed.scale;
            ed.cam_y -= d.y / ed.scale;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (ed.tool == TOOL_WALL) {
                wall_click(wx, wy);
            } else if (ed.tool == TOOL_SELECT) {
                pick_object(wx, wy);
                ed.section_selected = -1;
                if (ed.selected >= 0) {
                    obj_t* o = &ed.level.objs[ed.selected];
                    ed.wall_selected = -1;
                    ed.wall_drag = -1;
                    ed.section_selected = -1;
                    ed.sec_grab = 0;
                    ed.link_selected = -1;
                    ed.obj_grab = 0;
                    if (o->kind == OBJ_OBSTACLE || o->kind == OBJ_OPENABLE) {
                        float rx = (float)o->x, ry = (float)o->y;
                        float x1 = (float)(o->x + (o->w > 0 ? o->w : 1));
                        float y1 = (float)(o->y + (o->h > 0 ? o->h : 1));
                        const float ccx[4] = { rx, x1, rx, x1 };
                        const float ccy[4] = { ry, ry, y1, y1 };
                        const float cfx[4] = { x1, rx, x1, rx };
                        const float cfy[4] = { y1, y1, ry, ry };
                        for (int c = 0; c < 4; c++) {
                            if (fabsf(wx - ccx[c]) <= 0.6f && fabsf(wy - ccy[c]) <= 0.6f) {
                                ed.obj_grab = 2;
                                ed.obj_fx = cfx[c];
                                ed.obj_fy = cfy[c];
                                break;
                            }
                        }
                        if (ed.obj_grab == 0) {
                            ed.obj_grab = 1;
                        }
                    }
                    ed.drag_dx = (int)floorf(wx) - o->x;
                    ed.drag_dy = (int)floorf(wy) - o->y;
                    if (ed.obj_grab == 2) {
                        ed.drag_obj = -1;
                    }
                } else {
                    pick_wall(wx, wy);
                    ed.drag_dx = 0;
                    ed.drag_dy = 0;
                    ed.link_selected = -1;
                    if (ed.wall_selected >= 0) {
                        ed.section_selected = -1;
                        ed.sec_grab = 0;
                    } else {
                        pick_link(wx, wy);
                        if (ed.link_selected >= 0) {
                            ed.section_selected = -1;
                            ed.sec_grab = 0;
                        } else {
                            ed.section_selected = -1;
                            ed.sec_grab = 0;
                            for (int si = ed.level.section_count - 1; si >= 0; si--) {
                            section_t* s = &ed.level.sections[si];
                            float rx = (float)s->x, ry = (float)s->y;
                            float x1 = (float)(s->x + s->w), y1 = (float)(s->y + s->h);
                            const float cx[4] = { rx, x1, rx, x1 };
                            const float cy[4] = { ry, ry, y1, y1 };
                            const float fx[4] = { x1, rx, x1, rx };
                            const float fy[4] = { y1, y1, ry, ry };
                            int hit = -1;
                            for (int c = 0; c < 4; c++) {
                                if (fabsf(wx - cx[c]) <= 0.6f && fabsf(wy - cy[c]) <= 0.6f) {
                                    hit = c;
                                    break;
                                }
                            }
                            if (hit >= 0) {
                                ed.section_selected = si;
                                ed.sec_grab = 2;
                                ed.sec_fx = fx[hit];
                                ed.sec_fy = fy[hit];
                                break;
                            }
                            if (wx >= rx && wx <= x1 && wy >= ry && wy <= y1) {
                                ed.section_selected = si;
                                ed.sec_grab = 1;
                                ed.sec_dx = wx - rx;
                                ed.sec_dy = wy - ry;
                                break;
                            }
                        }
                    }
                }
                }
                ed.drag_obj = (ed.obj_grab == 2) ? -1 : ed.selected;
            } else if (ed.tool == TOOL_PLACE) {
                if (ed.place_kind == OBJ_DOOR) {
                    place_door(wx, wy);
                } else {
                    add_object(ed.place_kind, (int)floorf(wx), (int)floorf(wy));
                }
            } else if (ed.tool == TOOL_LINK) {
                pick_object(wx, wy);
                if (ed.selected >= 0) {
                    obj_t* o = &ed.level.objs[ed.selected];
                    if (ed.link_src == 0) {
                        ed.link_src = o->id;
                    } else if (o->id != ed.link_src) {
                        o->link_id = ed.link_src;
                        ed.link_src = 0;
    ed.link_selected = -1;
                        ed.dirty = true;
                    } else {
                        ed.link_src = 0;
    ed.link_selected = -1;
                    }
                }
            }
        }
        // dragging a selected object moves it to the cell under the cursor
        if (ed.drag_obj >= 0 && ed.drag_obj < ed.level.obj_count) {
            obj_t* o = &ed.level.objs[ed.drag_obj];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                int nx = (int)floorf(wx) - ed.drag_dx;
                int ny = (int)floorf(wy) - ed.drag_dy;
                if (o->kind == OBJ_DOOR) {
                    if (o->horizontal) {
                        ny = (int)floorf(wy + 0.5f);
                    } else {
                        nx = (int)floorf(wx + 0.5f);
                    }
                }
                if (nx != o->x || ny != o->y) {
                    o->x = nx;
                    o->y = ny;
                    ed.dirty = true;
                }
            } else {
                ed.drag_obj = -1;
            }
        }
        // resizing a rectangle object (obstacle/openable) from a corner
        if (ed.obj_grab == 2 && ed.selected >= 0 && ed.selected < ed.level.obj_count) {
            obj_t* o = &ed.level.objs[ed.selected];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                int mx = (int)floorf(wx + 0.5f);
                int my = (int)floorf(wy + 0.5f);
                int fx = (int)ed.obj_fx;
                int fy = (int)ed.obj_fy;
                int nx = mx < fx ? mx : fx;
                int ny = my < fy ? my : fy;
                int nw = (mx > fx ? mx : fx) - nx;
                int nh = (my > fy ? my : fy) - ny;
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                if (o->x != nx || o->y != ny || o->w != nw || o->h != nh) {
                    o->x = nx;
                    o->y = ny;
                    o->w = nw;
                    o->h = nh;
                    ed.dirty = true;
                }
            } else {
                ed.obj_grab = 0;
            }
        }
        // dragging a section moves it, or resizes it from the corner handle
        if (ed.sec_grab && ed.section_selected >= 0 && ed.section_selected < ed.level.section_count) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                section_t* s = &ed.level.sections[ed.section_selected];
                if (ed.sec_grab == 1) {
                    int nx = (int)floorf(wx - ed.sec_dx + 0.5f);
                    int ny = (int)floorf(wy - ed.sec_dy + 0.5f);
                    if (nx != s->x || ny != s->y) {
                        s->x = nx;
                        s->y = ny;
                        ed.dirty = true;
                    }
                } else {
                    int mx = (int)floorf(wx + 0.5f);
                    int my = (int)floorf(wy + 0.5f);
                    int fx = (int)ed.sec_fx;
                    int fy = (int)ed.sec_fy;
                    int nx = mx < fx ? mx : fx;
                    int ny = my < fy ? my : fy;
                    int nw = (mx > fx ? mx : fx) - nx;
                    int nh = (my > fy ? my : fy) - ny;
                    if (nw < 1) nw = 1;
                    if (nh < 1) nh = 1;
                    if (s->x != nx || s->y != ny || s->w != nw || s->h != nh) {
                        s->x = nx;
                        s->y = ny;
                        s->w = nw;
                        s->h = nh;
                        ed.dirty = true;
                    }
                }
            } else {
                ed.sec_grab = 0;
            }
        }
        // dragging a selected wall endpoint snaps it to the grid
        if (ed.wall_selected >= 0 && ed.wall_selected < ed.level.wall_count && ed.wall_drag >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                wall_seg_t* w = &ed.level.walls[ed.wall_selected];
                int nx = (int)floorf(wx + 0.5f);
                int ny = (int)floorf(wy + 0.5f);
                if (ed.wall_drag == 0) {
                    if (w->x0 != nx || w->y0 != ny) {
                        w->x0 = nx;
                        w->y0 = ny;
                        ed.dirty = true;
                    }
                } else {
                    if (w->x1 != nx || w->y1 != ny) {
                        w->x1 = nx;
                        w->y1 = ny;
                        ed.dirty = true;
                    }
                }
            } else {
                ed.wall_drag = -1;
                prune_walls();
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ed.wall_active = false;
            ed.link_src = 0;
    ed.link_selected = -1;
        }
        // [ and ] cycle the object kind, , and . cycle the item type
        if (ed.tool == TOOL_PLACE) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                ed.place_kind = (ed.place_kind + OBJ_KIND_COUNT - 1) % OBJ_KIND_COUNT;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                ed.place_kind = (ed.place_kind + 1) % OBJ_KIND_COUNT;
            }
            if (ed.place_kind == OBJ_ITEM) {
                if (ImGui::IsKeyPressed(ImGuiKey_Comma)) {
                    ed.place_item = (ed.place_item + ITEM_COUNT - 1) % ITEM_COUNT;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Period)) {
                    ed.place_item = (ed.place_item + 1) % ITEM_COUNT;
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (ed.wall_selected >= 0) {
                delete_wall();
            } else if (ed.link_selected >= 0) {
                delete_link();
            } else if (ed.section_selected >= 0) {
                delete_section();
            } else {
                delete_selected();
            }
        }
        // R rotates a door: the selected one if there is one, else the next placed
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            if (ed.selected >= 0 && ed.selected < ed.level.obj_count &&
                ed.level.objs[ed.selected].kind == OBJ_DOOR) {
                obj_t* o = &ed.level.objs[ed.selected];
                o->horizontal = !o->horizontal;
                ed.dirty = true;
            } else if (ed.tool == TOOL_PLACE && ed.place_kind == OBJ_DOOR) {
                ed.place_horizontal = !ed.place_horizontal;
            }
        }
    }
}

static void init(void) {
    sg_desc sgd = {};
    sgd.environment = sglue_environment();
    sgd.logger.func = slog_func;
    sg_setup(&sgd);
    sgl_desc_t sgld = {};
    sgld.logger.func = slog_func;
    sgl_setup(&sgld);
    simgui_desc_t simgui_desc = {};
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);
    ImGui::StyleColorsLight();  // panels on paper, like the game's UI

    ed.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ed.pass_action.colors[0].clear_value = { 0.85f, 0.85f, 0.85f, 1.0f };  // outside the level
    ed.tool = TOOL_SELECT;
    ed.place_kind = OBJ_DOOR;
    ed.place_item = ITEM_HERB;
    ed.place_horizontal = 1;
    ed.selected = -1;
    ed.section_selected = -1;
    ed.sec_grab = 0;
    ed.drag_obj = -1;
    ed.drag_dx = 0;
    ed.drag_dy = 0;
    ed.wall_selected = -1;
    ed.wall_drag = -1;
    ed.link_src = 0;
    ed.link_selected = -1;
    ed.show_sections = true;
    ed.ref_loaded = false;
    ed.ref_show = true;
    ed.ref_w = 0;
    ed.ref_h = 0;
    ed.ref_smp.id = 0;
    ed.ref_scale = 0.125f;
    ed.ref_x = -1353.0f * 0.125f;
    ed.ref_y = -926.0f * 0.125f;
    ed.ref_alpha = 0.6f;
    snprintf(ed.ref_path, sizeof(ed.ref_path), "../ref/map.png");
    ed.wall_active = false;
    ed.dirty = false;
    resolve_level_path();
    load_level();
    {
        // default the reference image to the repo's ref/map.png
        char tmp[600];
        snprintf(tmp, sizeof(tmp), "%s", ed.path);
        char* sl = strrchr(tmp, '/');
        if (sl) {
            *sl = 0;
            snprintf(ed.ref_path, sizeof(ed.ref_path), "%s/../../ref/map.png", tmp);
        }
    }
    fit_view();
}

static void frame(void) {
    simgui_frame_desc_t fd = {};
    fd.width = sapp_width();
    fd.height = sapp_height();
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&fd);
    handle_input();

    draw_map();
    ui_toolbar();
    ui_properties();
    ImGui::Render();
    simgui_flush();

    sg_pass pass = {};
    pass.action = ed.pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    sgl_draw();
    simgui_draw();
    sg_end_pass();
    sg_commit();
}

static void event(const sapp_event* e) {
    simgui_handle_event(e);
}

static void cleanup(void) {
    if (ed.ref_loaded) {
        sg_destroy_view(ed.ref_view);
        sg_destroy_image(ed.ref_img);
    }
    if (ed.ref_smp.id != 0) {
        sg_destroy_sampler(ed.ref_smp);
    }
    simgui_shutdown();
    sgl_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.event_cb = event;
    desc.cleanup_cb = cleanup;
    desc.width = 1280;
    desc.height = 800;
    desc.window_title = "sokol-ink-ribbon level editor";
    desc.icon.sokol_default = true;
    desc.logger.func = slog_func;
    return desc;
}
