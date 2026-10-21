//------------------------------------------------------------------------------
//  editor.cc
//  Dear ImGui level editor: paint the tile map (floor, the four room corners,
//  straight walls), drop doors and items, run level scripts, and save/load
//  the level JSON. Every brush stroke runs as a script command, so what you
//  paint can be replayed from a script file. Selecting, deleting (Del) and
//  dragging tiles and objects run script commands too.
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"
#include "imgui.h"
#include "imgui_internal.h"
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"

#include "../src/level.h"
#include "../src/levelscript.h"
#include "../src/light.h"
#include "../src/palette.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TOOL_SELECT = 0,
    TOOL_TILE,
    TOOL_PLACE,
    TOOL_LINK,
};

// Tile brushes are the tile kinds; the extras below them place objects or
// drag out a whole room.
enum {
    BRUSH_DOOR = TILE_KIND_COUNT,
    BRUSH_ITEM,
    BRUSH_ROOM,
    BRUSH_COUNT,
};

static struct {
    sg_pass_action pass_action;
    sgl_pipeline alpha_pip;  // alpha blending: the wall tint and overlays
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
    int brush;
    int room_x0, room_y0;
    bool room_drag;
    char script[16384];
    char script_path[512];
    char script_log[16384];
    bool docked;
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
    float cam_x, cam_y, scale;
    bool panning;
    float pan_x, pan_y;
    bool dirty;
    char status[640];
    // a selected tile (Select tool): Del voids it, dragging moves it
    bool tile_sel;
    int tile_x, tile_y;
    bool tile_drag;
    // where a dragged object started, so the drop runs as one move command
    int drag_from_x, drag_from_y;
    // --idcheck: sweep the mouse over every panel and count ImGui ID clashes
    bool idcheck;
    int idcheck_frame;
    int idcheck_conflicts;
    int idcheck_states;  // --idcheck N: only the first N states (0 = all)
    // --shot FILE: write the next frames' framebuffer (for the screenshots)
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

// The Player Spawn object, or NULL.
static obj_t* player_spawn(void) {
    for (int i = 0; i < ed.level.obj_count; i++)
        if (ed.level.objs[i].kind == OBJ_START) return &ed.level.objs[i];
    return NULL;
}

static void add_object(int kind, int x, int y) {
    if (kind == OBJ_START && player_spawn()) {
        // one Player Spawn per level: placing another moves it
        obj_t* s = player_spawn();
        s->x = ed.level.start_x = x;
        s->y = ed.level.start_y = y;
        ed.selected = (int)(s - ed.level.objs);
        ed.dirty = true;
        snprintf(ed.status, sizeof(ed.status), "One Player Spawn per level: moved it to %d,%d", x, y);
        return;
    }
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
        o->span = DOOR_SPAN;
    }
    if (kind == OBJ_OBSTACLE) {
        o->w = 2;
        o->h = 2;
    }
    snprintf(o->name, LEVEL_MAX_NAME, "%s", obj_kind_name(o->kind));
    if (kind == OBJ_START) {
        snprintf(o->name, LEVEL_MAX_NAME, "Grace start");
        ed.level.start_x = x;
        ed.level.start_y = y;
    }
    ed.selected = ed.level.obj_count - 1;
    ed.dirty = true;
}

static bool run_command(const char* fmt, ...);

// Del on a selected object: one script command, so it replays.
static void delete_selected(void) {
    if (ed.selected < 0 || ed.selected >= ed.level.obj_count) {
        return;
    }
    int id = ed.level.objs[ed.selected].id;
    ed.selected = -1;
    run_command("delete %d", id);
}

// Del on a selected tile: true void (paper, like the game), not a grey cell.
static void delete_tile(void) {
    if (!ed.tile_sel) {
        return;
    }
    ed.tile_sel = false;
    ed.tile_drag = false;
    run_command("tile void %d %d", ed.tile_x, ed.tile_y);
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
        } else if (o->kind == OBJ_OBSTACLE) {
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


// Tiles draw the way the game draws the level: floor and void are both
// paper (the game's background), and the walls are the ink lines between
// floor and everything else (ed.level.walls, compiled from the tiles, the
// same edges the game draws). Solid wall tiles get a faint tint and glyph
// so they can be seen and picked while editing; void gets nothing.
static void draw_tile(tile_t t, float sx, float sy, float sc) {
    if (t == TILE_VOID || t == TILE_FLOOR) {
        return;
    }
    fill_rect(sx, sy, sc, sc, PAL_GRAY, 0.22f);
    float th = sc * 0.16f;
    float mid = (sc - th) * 0.5f;
    bool l = t == TILE_WALL_H || t == TILE_CORNER_NE || t == TILE_CORNER_SE;
    bool r = t == TILE_WALL_H || t == TILE_CORNER_NW || t == TILE_CORNER_SW;
    bool u = t == TILE_WALL_V || t == TILE_CORNER_SW || t == TILE_CORNER_SE;
    bool d = t == TILE_WALL_V || t == TILE_CORNER_NW || t == TILE_CORNER_NE;
    fill_rect(sx + mid, sy + mid, th, th, PAL_GRAY, 0.6f);
    if (l) fill_rect(sx, sy + mid, mid, th, PAL_GRAY, 0.6f);
    if (r) fill_rect(sx + mid + th, sy + mid, mid, th, PAL_GRAY, 0.6f);
    if (u) fill_rect(sx + mid, sy, th, mid, PAL_GRAY, 0.6f);
    if (d) fill_rect(sx + mid, sy + mid + th, th, mid, PAL_GRAY, 0.6f);
}

static void draw_walls(void) {
    float t = PAL_WALL_PX;
    for (int i = 0; i < ed.level.wall_count; i++) {
        const wall_seg_t* w = &ed.level.walls[i];
        float x0 = view_x((float)w->x0), y0 = view_y((float)w->y0);
        float x1 = view_x((float)w->x1), y1 = view_y((float)w->y1);
        if (w->y0 == w->y1) {
            fill_rect(x0, y0 - t * 0.5f, x1 - x0, t, PAL_INK, 1.0f);
        } else {
            fill_rect(x0 - t * 0.5f, y0, t, y1 - y0, PAL_INK, 1.0f);
        }
    }
}

static void log_line(const char* line) {
    size_t used = strlen(ed.script_log);
    size_t need = strlen(line) + 1;
    if (used + need + 1 > sizeof(ed.script_log)) {
        // keep the newest half
        size_t drop = used / 2;
        const char* nl = strchr(ed.script_log + drop, '\n');
        drop = nl ? (size_t)(nl - ed.script_log) + 1 : used;
        memmove(ed.script_log, ed.script_log + drop, used - drop + 1);
        used -= drop;
    }
    snprintf(ed.script_log + used, sizeof(ed.script_log) - used, "%s\n", line);
}

// The one way the editor changes tiles and drops doors/items: run script
// text, so the tool panel, the script panel and script files all share it.
static bool run_script(const char* text, bool log) {
    char err[256];
    int ran = levelscript_run(&ed.level, text, err, sizeof(err));
    if (ran < 0) {
        snprintf(ed.status, sizeof(ed.status), "script error: %s", err);
        return false;
    }
    if (log) {
        log_line(text);
    }
    ed.dirty = true;
    snprintf(ed.status, sizeof(ed.status), "ran %d command%s", ran, ran == 1 ? "" : "s");
    return true;
}

static bool run_command(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    return run_script(line, true);
}

static bool run_script_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(ed.status, sizeof(ed.status), "cannot read %s", path);
        return false;
    }
    static char buf[1 << 20];
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;
    if (!run_script(buf, false)) {
        return false;
    }
    char note[600];
    snprintf(note, sizeof(note), "# ran %s", path);
    log_line(note);
    return true;
}

static void brush_paint(int x, int y) {
    if (x < 0 || y < 0 || x >= ed.level.cols || y >= ed.level.rows) {
        return;
    }
    if (ed.level.has_tiles && level_tile(&ed.level, x, y) == (tile_t)ed.brush) {
        return;
    }
    run_command("tile %s %d %d", tile_name((tile_t)ed.brush), x, y);
}

static bool is_wall_tile(int x, int y) {
    return level_tile(&ed.level, x, y) >= TILE_CORNER_NW;
}

static void brush_press(int x, int y) {
    if (ed.brush < TILE_KIND_COUNT) {
        brush_paint(x, y);
    } else if (ed.brush == BRUSH_DOOR) {
        // snap to the doorway under the cursor: a 2-cell gap gets a normal
        // door, any other gap a barrier of its width; on a solid wall it
        // cuts a normal span-2 door along that wall
        int fx, fy, fh, fs;
        if (level_door_fit(&ed.level, x, y, &fx, &fy, &fh, &fs)) {
            if (fs == DOOR_SPAN) {
                run_command("door %d %d", x, y);
            } else {
                run_command("door %d %d name=barrier", x, y);
            }
        } else {
            bool h = is_wall_tile(x - 1, y) || is_wall_tile(x + 1, y);
            run_command("door %d %d %c", x, y, h ? 'h' : 'v');
        }
    } else if (ed.brush == BRUSH_ITEM) {
        run_command("item %d %d %s", x, y, item_name((item_t)ed.place_item));
    } else if (ed.brush == BRUSH_ROOM) {
        ed.room_drag = true;
        ed.room_x0 = x;
        ed.room_y0 = y;
    }
}

static void draw_map(void) {
    sgl_defaults();
    sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
    sgl_load_pipeline(ed.alpha_pip);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, sapp_widthf(), sapp_heightf(), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    int cols = ed.level.cols;
    int rows = ed.level.rows;
    fill_rect(view_x(0.0f), view_y(0.0f), (float)cols * ed.scale, (float)rows * ed.scale, PAL_PAPER, 1.0f);
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
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            draw_tile(level_tile(&ed.level, x, y), view_x((float)x), view_y((float)y), ed.scale);
        }
    }
    draw_walls();
    if (ed.tile_sel) {
        float sx = view_x((float)ed.tile_x), sy = view_y((float)ed.tile_y), sc = ed.scale;
        draw_line(sx, sy, sx + sc, sy, 2.0f, PAL_CYAN, 1);
        draw_line(sx + sc, sy, sx + sc, sy + sc, 2.0f, PAL_CYAN, 1);
        draw_line(sx + sc, sy + sc, sx, sy + sc, 2.0f, PAL_CYAN, 1);
        draw_line(sx, sy + sc, sx, sy, 2.0f, PAL_CYAN, 1);
        if (ed.tile_drag) {
            ImVec2 m = ImGui::GetIO().MousePos;
            float wx, wy;
            screen_to_world(m.x, m.y, &wx, &wy);
            float dx = view_x(floorf(wx)), dy = view_y(floorf(wy));
            draw_tile(level_tile(&ed.level, ed.tile_x, ed.tile_y), dx, dy, sc);
            draw_line(dx, dy, dx + sc, dy, 2.0f, 1.0f, 0.55f, 0.0f, 1);
            draw_line(dx + sc, dy, dx + sc, dy + sc, 2.0f, 1.0f, 0.55f, 0.0f, 1);
            draw_line(dx + sc, dy + sc, dx, dy + sc, 2.0f, 1.0f, 0.55f, 0.0f, 1);
            draw_line(dx, dy + sc, dx, dy, 2.0f, 1.0f, 0.55f, 0.0f, 1);
        }
    }
    if (ed.room_drag) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float wx, wy;
        screen_to_world(m.x, m.y, &wx, &wy);
        float x0 = view_x((float)(ed.room_x0 < (int)floorf(wx) ? ed.room_x0 : (int)floorf(wx)));
        float y0 = view_y((float)(ed.room_y0 < (int)floorf(wy) ? ed.room_y0 : (int)floorf(wy)));
        float w = (fabsf(floorf(wx) - (float)ed.room_x0) + 1.0f) * ed.scale;
        float h = (fabsf(floorf(wy) - (float)ed.room_y0) + 1.0f) * ed.scale;
        fill_rect(x0, y0, w, h, PAL_CYAN, 0.25f);
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
            float k = o->state == LIGHT_ON ? 1.0f : 0.45f;
            for (int seg = 0; seg < 64; seg++) {
                float a0 = (float)seg / 64.0f * 6.2831853f, a1 = (float)(seg + 1) / 64.0f * 6.2831853f;
                draw_line(cx + cosf(a0) * lr, cy + sinf(a0) * lr, cx + cosf(a1) * lr, cy + sinf(a1) * lr,
                          picked ? 3.0f : 2.0f, 0.8f * k, 0.84f * k, 0.0f, 1.0f);  // the game's yellow, darkened to read on paper
            }
            if (o->state == LIGHT_ON) {
                fill_circle(cx, cy, scale * 0.55f, 0.949f, 1.0f, 0.0f, 0.18f);
                fill_circle(cx, cy, scale * 0.35f, 0.949f, 1.0f, 0.0f, 0.35f);
                fill_circle(cx, cy, scale * 0.33f, 0.118f, 0.118f, 0.118f, 1.0f);
                fill_circle(cx, cy, scale * 0.28f, 1.0f, 0.95f, 0.2f, 1.0f);
            } else {
                fill_circle(cx, cy, scale * 0.33f, 0.118f, 0.118f, 0.118f, 1.0f);
                fill_circle(cx, cy, scale * 0.28f, 0.616f, 0.616f, 0.616f, 1.0f);
            }
        } else if (o->kind == OBJ_OBSTACLE) {
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
        if (i == ed.selected && o->kind != OBJ_OBSTACLE) {
                fill_rect(rx - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx + rw - 5, ry - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
                fill_rect(rx + rw - 5, ry + rh - 5, 10, 10, 1.0f, 0.55f, 0.0f, 1.0f);
            }
        } else if (o->kind == OBJ_START) {
            fill_circle(cx, cy, scale * 0.51f, PAL_PURPLE, 1.0f);  // Grace's dot
        } else if (o->kind == OBJ_ENEMY_SPAWN) {
            // the enemy's dark red dot, ringed in ink: it spawns exactly here
            fill_circle(cx, cy, scale * 0.62f, PAL_INK, 1.0f);
            fill_circle(cx, cy, scale * 0.51f, 0.62f, 0.05f, 0.05f, 1.0f);
        } else {
            float r = 0.6f, g = 0.6f, b = 0.6f;
            switch (o->kind) {
                case OBJ_SWITCH: r = 0.949f; g = 1.0f; b = 0.0f; break;
                case OBJ_TYPEWRITER: r = 1.0f; g = 0.0f; b = 0.416f; break;
                case OBJ_FILE: r = 0.85f; g = 0.85f; b = 0.85f; break;  // white in the game, a shade darker here to show on paper
                case OBJ_SAFE: r = 0.616f; g = 0.616f; b = 0.616f; break;
                case OBJ_OBSTACLE: r = 0.5f; g = 0.45f; b = 0.4f; break;
                case OBJ_FUSEBOX: r = 0.118f; g = 0.118f; b = 0.118f; break;
                case OBJ_START: r = 0.588f; g = 0.118f; b = 1.0f; break;
                case OBJ_GOAL: r = 0.118f; g = 0.118f; b = 0.118f; break;
                case OBJ_RECIPE: r = 1.0f; g = 1.0f; b = 1.0f; break;    // paper sheet
                case OBJ_CABINET: r = 0.616f; g = 0.616f; b = 0.616f; break;  // PAL_GRAY
                case OBJ_ITEMBOX: r = 0.45f; g = 0.30f; b = 0.15f; break;    // the brown trunk
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
    ed.drag_obj = -1;
    ed.drag_dx = 0;
    ed.drag_dy = 0;
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

// A short hover tooltip on the widget just drawn.
static void tip(const char* text) {
    ImGui::SetItemTooltip("%s", text);
}

static void ui_toolbar(void) {
    ImGui::Begin("Tools");
    if (ImGui::Button("Save")) {
        save_level();
    }
    tip("Write the level JSON to its file");
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        load_level();
    }
    tip("Reload the level from its file, dropping unsaved edits");
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        new_level();
    }
    tip("Start an empty level of the same size");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        fit_view();
    }
    tip("Zoom and pan so the whole level is in view");
    ImGui::Separator();
    ImGui::Text("Tool");
    ImGui::RadioButton("Select", &ed.tool, TOOL_SELECT);
    tip("Click a tile, object, link or section to select it.\nDrag to move it, Del to delete it.");
    ImGui::RadioButton("Tiles", &ed.tool, TOOL_TILE);
    tip("Paint with the brush picked in the Tiles panel");
    ImGui::RadioButton("Place", &ed.tool, TOOL_PLACE);
    tip("Click to place one object of the Kind below ([ and ] cycle it)");
    ImGui::RadioButton("Link", &ed.tool, TOOL_LINK);
    tip("Click the source, then the object it drives (switch to light, key to door)");
    if (ImGui::Button("Light")) {
        ed.tool = TOOL_PLACE;
        ed.place_kind = OBJ_LIGHT;
    }
    tip("Place a light");
    ImGui::SameLine();
    if (ImGui::Button("Switch")) {
        ed.tool = TOOL_PLACE;
        ed.place_kind = OBJ_SWITCH;
    }
    tip("Place a light switch");
    if (ed.tool == TOOL_PLACE) {
        const char* kinds[OBJ_KIND_COUNT];
        for (int i = 0; i < OBJ_KIND_COUNT; i++) {
            kinds[i] = obj_kind_name((obj_kind_t)i);
        }
        ImGui::Combo("Kind", &ed.place_kind, kinds, OBJ_KIND_COUNT);
        tip("The object the Place tool drops");
        if (ed.place_kind == OBJ_ITEM) {
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) {
                items[i] = item_name((item_t)i);
            }
            ImGui::Combo("Item", &ed.place_item, items, ITEM_COUNT);
            tip("The item type (, and . cycle it)");
        }
    }
    ImGui::Separator();
    ImGui::Text("Sections");
    ImGui::SameLine();
    ImGui::Checkbox("show", &ed.show_sections);
    tip("Draw the section outlines on the map");
    if (ImGui::Button("Add")) {
        add_section();
    }
    tip("Add a 10x10 section at the top-left corner");
    ImGui::SameLine();
    if (ImGui::Button("Delete##section")) {
        delete_section();
    }
    tip("Delete the selected section");
    // names repeat (two "F1 Corridor"s), so each row gets its index as its ID
    for (int i = 0; i < ed.level.section_count; i++) {
        const section_t* sec = &ed.level.sections[i];
        ImGui::PushID(i);
        if (ImGui::Selectable(sec->name, i == ed.section_selected)) {
            ed.section_selected = i;
            ed.selected = -1;
            ed.tile_sel = false;
        }
        ImGui::SetItemTooltip("%s: %d,%d  %dx%d", sec->name, sec->x, sec->y, sec->w, sec->h);
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::Text("Reference image");
    ImGui::InputText("path", ed.ref_path, sizeof(ed.ref_path));
    tip("Image file to trace over");
    if (ImGui::Button("Load")) {
        load_reference();
    }
    tip("Load the reference image");
    ImGui::SameLine();
    ImGui::Checkbox("show##ref", &ed.ref_show);
    tip("Show the reference image under the map");
    if (ed.ref_loaded) {
        ImGui::DragFloat("ref x", &ed.ref_x, 0.25f);
        tip("Reference image left edge, in cells (drag)");
        ImGui::DragFloat("ref y", &ed.ref_y, 0.25f);
        tip("Reference image top edge, in cells (drag)");
        ImGui::DragFloat("ref scale", &ed.ref_scale, 0.001f, 0.001f, 4.0f, "%.4f");
        tip("Cells per image pixel (drag)");
        ImGui::SliderFloat("opacity", &ed.ref_alpha, 0.0f, 1.0f);
        tip("How strongly the reference image shows");
    }
    ImGui::End();
}

static void ui_properties(void) {
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
            // Lock: what opens the door. Keys are item ids; 0 (the bottle,
            // never a key) reads as "none".
            const char* locks[LOCK_KIND_COUNT];
            for (int i = 0; i < LOCK_KIND_COUNT; i++) {
                locks[i] = door_lock_name(i);
            }
            changed |= ImGui::Combo("lock", &o->lock, locks, LOCK_KIND_COUNT);
            const char* keys[ITEM_COUNT];
            keys[0] = "(none)";
            for (int i = 1; i < ITEM_COUNT; i++) {
                keys[i] = item_name((item_t)i);
            }
            if (o->lock == LOCK_KEY) {
                changed |= ImGui::Combo("key", &o->key_id, keys, ITEM_COUNT);
                changed |= ImGui::Combo("key2", &o->key2_id, keys, ITEM_COUNT);
                changed |= ImGui::Combo("key3", &o->key3_id, keys, ITEM_COUNT);
            } else if (o->lock == LOCK_OTHER_SIDE) {
                int side = o->side < 0 ? 0 : 1;
                const char* sides[2] = { "before (y-1 / x-1)", "after (y / x)" };
                if (ImGui::Combo("opens from", &side, sides, 2)) {
                    o->side = side ? 1 : -1;
                    changed = true;
                }
            } else if (o->lock == LOCK_NONE) {
                changed |= ImGui::InputInt("key_id", &o->key_id);
            }
            // a normal door is always DOOR_SPAN wide; barriers and gates fit their gap
            if (strstr(o->name, "barrier") || strstr(o->name, "gate")) {
                changed |= ImGui::InputInt("span", &o->span);
                tip("Barrier width in cells (the Door tool fits it to the gap)");
            } else {
                ImGui::Text("span %d (fixed)", o->span);
                tip("Normal doors are 2 cells wide, like Room 202's door");
            }
            changed |= ImGui::InputFloat("auto_close", &o->auto_close);
            changed |= ImGui::Checkbox("breakable", (bool*)&o->breakable);
        } else if (o->kind == OBJ_ITEM) {
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) {
                items[i] = item_name((item_t)i);
            }
            changed |= ImGui::Combo("item", &o->item_type, items, ITEM_COUNT);
            changed |= ImGui::InputInt("qty", &o->qty);
            ImGui::TextWrapped("%s", item_text(o->item_type));
        } else if (o->kind == OBJ_LIGHT) {
            changed |= ImGui::SliderFloat("light radius", &o->radius, LIGHT_RADIUS_MIN, LIGHT_RADIUS_MAX, "%.1f cells");
            tip("How far the light reaches (the game also stops it at walls)");
            ImGui::TextDisabled("game: unlit items are hidden");
            ImGui::TextDisabled("link: the switch that toggles it");
            if (o->state == LIGHT_BROKEN) {
                ImGui::TextDisabled("broken");
            } else {
                bool on = o->state == LIGHT_ON;
                if (ImGui::Checkbox("on", &on)) {
                    o->state = on ? LIGHT_ON : LIGHT_OFF;
                    changed = true;
                }
            }
            changed |= ImGui::InputInt("group_id", &o->group_id);
            changed |= ImGui::Checkbox("breakable", (bool*)&o->breakable);
        } else if (o->kind == OBJ_SWITCH) {
            changed |= ImGui::InputInt("group_id", &o->group_id);
        } else if (o->kind == OBJ_OBSTACLE) {
            changed |= ImGui::InputInt("w", &o->w);
            changed |= ImGui::InputInt("h", &o->h);
            changed |= ImGui::Checkbox("climbable", (bool*)&o->climbable);
            changed |= ImGui::Checkbox("movable", (bool*)&o->movable);
            changed |= ImGui::Checkbox("openable", (bool*)&o->openable);
            if (o->openable) changed |= ImGui::Checkbox("open", (bool*)&o->open);
        } else if (o->kind == OBJ_FILE || o->kind == OBJ_SAFE) {
            changed |= ImGui::InputInt("code", &o->code);
            if (o->kind == OBJ_SAFE) {
                // a puzzle box: holding this item opens it (no puzzle)
                const char* keys[ITEM_COUNT];
                keys[0] = "(code instead)";
                for (int i = 1; i < ITEM_COUNT; i++) keys[i] = item_name((item_t)i);
                changed |= ImGui::Combo("opened by", &o->key_id, keys, ITEM_COUNT);
            }
        } else if (o->kind == OBJ_CABINET) {
            // a coin cabinet: `code` Antique Coins buy item x qty (qty 0:
            // +4 inventory slots instead)
            changed |= ImGui::InputInt("price (coins)", &o->code);
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) items[i] = item_name((item_t)i);
            changed |= ImGui::Combo("sells", &o->item_type, items, ITEM_COUNT);
            changed |= ImGui::InputInt("qty", &o->qty);
            if (o->qty <= 0) ImGui::TextDisabled("qty 0: sells +4 inventory slots");
        } else if (o->kind == OBJ_START) {
            ImGui::TextDisabled("Player Spawn: Grace starts here (one per level)");
        } else if (o->kind == OBJ_ENEMY_SPAWN) {
            ImGui::TextDisabled("Enemy Spawn: the enemy spawns exactly here.");
            ImGui::TextDisabled("No Enemy Spawn in the level: no enemy.");
        } else if (o->kind == OBJ_GOAL) {
            const char* items[ITEM_COUNT];
            for (int i = 0; i < ITEM_COUNT; i++) items[i] = item_name((item_t)i);
            changed |= ImGui::Combo("gives", &o->item_type, items, ITEM_COUNT);
            changed |= ImGui::InputInt("qty", &o->qty);
            ImGui::TextDisabled(o->qty > 0 ? "hands over the item once the fuse is in" : "qty 0: reaching it ends the level");
        } else if (o->kind == OBJ_ITEMBOX) {
            ImGui::TextDisabled("Item Box: every box shares one store");
        }
        changed |= ImGui::InputInt("link", &o->link_id);
        ed.dirty |= changed;
        if (ImGui::Button("Delete##object")) {
            delete_selected();
        }
        tip("Delete this object (Del)");
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
    } else if (ed.tile_sel) {
        ImGui::Text("Tile %d,%d: %s", ed.tile_x, ed.tile_y, tile_name(level_tile(&ed.level, ed.tile_x, ed.tile_y)));
        if (ImGui::Button("Delete##tile")) {
            delete_tile();
        }
        tip("Remove the tile completely, leaving void (Del)");
        ImGui::TextDisabled("drag it on the map to move it");
    } else {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::Text("Level");
        bool changed = false;
        changed |= ImGui::InputText("name", ed.level.name, LEVEL_MAX_NAME);
        ImGui::Text("Player Spawn");
        bool moved = ImGui::InputInt("start x", &ed.level.start_x);
        moved |= ImGui::InputInt("start y", &ed.level.start_y);
        if (moved && player_spawn()) {
            player_spawn()->x = ed.level.start_x;
            player_spawn()->y = ed.level.start_y;
        }
        changed |= moved;
        changed |= ImGui::InputInt("cols", &ed.level.cols);
        changed |= ImGui::InputInt("rows", &ed.level.rows);
        ed.dirty |= changed;
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", ed.status);
    ImGui::Text("zoom: %.1f px/cell%s", ed.scale, ed.dirty ? "  [modified]" : "");
    ImGui::End();
}

static void ui_tiles(void) {
    ImGui::Begin("Tiles");
    // No Void brush: select a tile and press Del to remove it (true void).
    static const char* labels[BRUSH_COUNT] = {
        "", "Floor", "Corner NW", "Corner NE", "Corner SW", "Corner SE",
        "Horizontal wall", "Vertical wall", "Door", "Item", "Room (drag)",
    };
    static const char* tips[BRUSH_COUNT] = {
        "",
        "Walkable floor (drag to paint)",
        "Wall corner opening to the south-east",
        "Wall corner opening to the south-west",
        "Wall corner opening to the north-east",
        "Wall corner opening to the north-west",
        "Straight wall running east-west",
        "Straight wall running north-south",
        "Click a doorway: snaps a 2-cell door flush between the jambs\n(other gap widths get a barrier); on a wall it cuts a 2-cell door",
        "Click to drop the item picked below",
        "Drag out a whole room: floor wrapped in walls",
    };
    for (int b = TILE_FLOOR; b < BRUSH_COUNT; b++) {
        if (ImGui::RadioButton(labels[b], ed.tool == TOOL_TILE && ed.brush == b)) {
            ed.tool = TOOL_TILE;
            ed.brush = b;
        }
        tip(tips[b]);
    }
    if (ed.brush == BRUSH_ITEM) {
        const char* items[ITEM_COUNT];
        for (int i = 0; i < ITEM_COUNT; i++) {
            items[i] = item_name((item_t)i);
        }
        ImGui::Combo("item##brush", &ed.place_item, items, ITEM_COUNT);
        tip("The item the Item brush drops");
    }
    ImGui::Separator();
    if (ImGui::Button("Auto walls")) {
        run_command("autowall");
    }
    tip("Wrap every floor cell in wall tiles and fix the corner glyphs");
    ImGui::SameLine();
    ImGui::TextDisabled("wrap floor in walls");
    ImGui::TextDisabled("Each stroke runs a script command;\nsee the Script log. To erase, select\na tile (Select tool) and press Del.");
    ImGui::End();
}

static void ui_script(void) {
    ImGui::Begin("Script");
    ImGui::InputTextMultiline("##script", ed.script, sizeof(ed.script), ImVec2(-1, ImGui::GetTextLineHeight() * 10));
    if (ImGui::Button("Run")) {
        run_script(ed.script, true);
    }
    tip("Run the script above against the level");
    ImGui::SameLine();
    ImGui::TextDisabled("one command per line");
    ImGui::InputText("file", ed.script_path, sizeof(ed.script_path));
    tip("Script file for Run file and Save log to file");
    if (ImGui::Button("Run file")) {
        run_script_file(ed.script_path);
    }
    tip("Run the script file");
    ImGui::SameLine();
    if (ImGui::Button("Save log to file")) {
        FILE* f = fopen(ed.script_path, "wb");
        if (f) {
            fputs(ed.script_log, f);
            fclose(f);
            snprintf(ed.status, sizeof(ed.status), "wrote log to %s", ed.script_path);
        } else {
            snprintf(ed.status, sizeof(ed.status), "cannot write %s", ed.script_path);
        }
    }
    tip("Write every command run so far to the script file, to replay later");
    ImGui::Separator();
    ImGui::Text("Log");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(ed.script_log);
    }
    tip("Copy the log to the clipboard");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        ed.script_log[0] = 0;
    }
    tip("Clear the log (the level is unchanged)");
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(ed.script_log);
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

// Left column: Tools over Tiles. Right column: Properties over Script. The
// map shows through the empty middle.
static void dock_panels(void) {
    ImGuiID root = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    if (ed.docked) {
        return;
    }
    ed.docked = true;
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->Size);
    ImGuiID left, rest, right, centre, left_bottom, right_bottom;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.22f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.30f, &right, &centre);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, &left_bottom, &left);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, &right_bottom, &right);
    ImGui::DockBuilderDockWindow("Tools", left);
    ImGui::DockBuilderDockWindow("Tiles", left_bottom);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Script", right_bottom);
    ImGui::DockBuilderFinish(root);
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
            if (ed.tool == TOOL_TILE) {
                brush_press((int)floorf(wx), (int)floorf(wy));
            } else if (ed.tool == TOOL_SELECT) {
                pick_object(wx, wy);
                ed.section_selected = -1;
                ed.tile_sel = false;
                ed.tile_drag = false;
                if (ed.selected >= 0) {
                    obj_t* o = &ed.level.objs[ed.selected];
                    ed.section_selected = -1;
                    ed.sec_grab = 0;
                    ed.link_selected = -1;
                    ed.obj_grab = 0;
                    if (o->kind == OBJ_OBSTACLE) {
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
                    ed.drag_from_x = o->x;
                    ed.drag_from_y = o->y;
                    if (ed.obj_grab == 2) {
                        ed.drag_obj = -1;
                    }
                } else {
                    ed.drag_dx = 0;
                    ed.drag_dy = 0;
                    ed.link_selected = -1;
                    {
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
                                // a painted tile inside wins over the section body
                                if (level_tile(&ed.level, (int)floorf(wx), (int)floorf(wy)) != TILE_VOID) {
                                    break;
                                }
                                ed.section_selected = si;
                                ed.sec_grab = 1;
                                ed.sec_dx = wx - rx;
                                ed.sec_dy = wy - ry;
                                break;
                            }
                        }
                        // nothing else hit: select the tile under the cursor
                        int tx = (int)floorf(wx), ty = (int)floorf(wy);
                        if (ed.section_selected < 0 && level_tile(&ed.level, tx, ty) != TILE_VOID) {
                            ed.tile_sel = true;
                            ed.tile_drag = true;
                            ed.tile_x = tx;
                            ed.tile_y = ty;
                        }
                    }
                }
                }
                ed.drag_obj = (ed.obj_grab == 2) ? -1 : ed.selected;
            } else if (ed.tool == TOOL_PLACE) {
                add_object(ed.place_kind, (int)floorf(wx), (int)floorf(wy));
                ed.tool = TOOL_SELECT;  // placed: back to Select
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
                // dropped: put it back and run the move as one command
                int tx = o->x, ty = o->y, id = o->id;
                ed.drag_obj = -1;
                if (tx != ed.drag_from_x || ty != ed.drag_from_y) {
                    o->x = ed.drag_from_x;
                    o->y = ed.drag_from_y;
                    run_command("move %d %d %d", id, tx, ty);
                }
            }
        }
        // dragging a selected tile carries it to the cell it is dropped on
        if (ed.tile_drag && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ed.tile_drag = false;
            int tx = (int)floorf(wx), ty = (int)floorf(wy);
            if (ed.tile_sel && (tx != ed.tile_x || ty != ed.tile_y) && tx >= 0 && ty >= 0 &&
                tx < ed.level.cols && ty < ed.level.rows) {
                run_command("movetile %d %d %d %d", ed.tile_x, ed.tile_y, tx, ty);
                ed.tile_x = tx;
                ed.tile_y = ty;
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
        // tile brushes paint while the button is held; the room brush drags
        if (ed.tool == TOOL_TILE && ed.brush < TILE_KIND_COUNT && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            brush_paint((int)floorf(wx), (int)floorf(wy));
        }
        if (ed.room_drag && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ed.room_drag = false;
            run_command("room %d %d %d %d", ed.room_x0, ed.room_y0, (int)floorf(wx), (int)floorf(wy));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
            if (ed.link_selected >= 0) {
                delete_link();
            } else if (ed.selected >= 0) {
                delete_selected();
            } else if (ed.tile_sel) {
                delete_tile();
            } else if (ed.section_selected >= 0) {
                delete_section();
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
    // a full 256x256 tile map plus objects and links
    sgld.max_vertices = 1 << 20;
    sgld.max_commands = 1 << 14;
    sgld.logger.func = slog_func;
    sgl_setup(&sgld);
    sg_pipeline_desc pd = {};
    pd.colors[0].blend.enabled = true;
    pd.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pd.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ed.alpha_pip = sgl_make_pipeline(&pd);
    simgui_desc_t simgui_desc = {};
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().IniFilename = NULL;  // always open on the docked layout
    ImGui::StyleColorsLight();  // panels on paper, like the game's UI

    ed.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ed.pass_action.colors[0].clear_value = { 0.85f, 0.85f, 0.85f, 1.0f };  // outside the level (inside is paper, like the game)
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
    ed.dirty = false;
    ed.brush = TILE_FLOOR;
    resolve_level_path();
    load_level();
    for (int i = 1; i + 1 < g_argc; i++) {
        if (strcmp(g_argv[i], "--script") == 0) {
            snprintf(ed.script_path, sizeof(ed.script_path), "%s", g_argv[i + 1]);
            run_script_file(ed.script_path);
        }
    }
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

// --idcheck: a self-test for ImGui ID clashes. ImGui only reports a clash
// for the item under the mouse, so this sweeps the mouse down columns over
// both panel stacks in several editor states (nothing / a section / a tile /
// one object of each kind selected, the Place and Item tools) and counts the
// frames where ImGui flags a conflict. Exit code 1 if any.
enum { IDCHECK_STEP = 12 };  // under one row of ImGui text, so every row is hit
static const float k_idcheck_x[] = { 20.0f, 70.0f, 140.0f, 230.0f, -330.0f, -240.0f, -120.0f, -30.0f };
#define IDCHECK_COLS ((int)(sizeof(k_idcheck_x) / sizeof(k_idcheck_x[0])))

static int idcheck_rows(void) {
    return sapp_height() / IDCHECK_STEP;
}

// Set up editor state number st; false when there are no more states.
static bool idcheck_state(int st) {
    ed.selected = -1;
    ed.section_selected = -1;
    ed.tile_sel = false;
    ed.tool = TOOL_SELECT;
    if (st == 0) return true;
    if (st == 1) { ed.section_selected = 0; return true; }
    if (st == 2) {
        for (int y = 0; y < ed.level.rows && !ed.tile_sel; y++)
            for (int x = 0; x < ed.level.cols && !ed.tile_sel; x++)
                if (level_tile(&ed.level, x, y) != TILE_VOID) { ed.tile_sel = true; ed.tile_x = x; ed.tile_y = y; }
        return true;
    }
    if (st == 3) { ed.tool = TOOL_PLACE; ed.place_kind = OBJ_ITEM; return true; }
    if (st == 4) { ed.tool = TOOL_TILE; ed.brush = BRUSH_ITEM; return true; }
    int kind = st - 5;
    if (kind >= OBJ_KIND_COUNT) return false;
    for (int i = 0; i < ed.level.obj_count; i++)
        if (ed.level.objs[i].kind == kind) { ed.selected = i; break; }
    return true;
}

static void idcheck_before_frame(void) {
    int f = ed.idcheck_frame++;
    int per_col = idcheck_rows() * 2;  // two frames per spot: hover, then the check
    int per_state = per_col * IDCHECK_COLS;
    int st = f / per_state;
    int in = f % per_state;
    if (in == 0 && ((ed.idcheck_states > 0 && st >= ed.idcheck_states) || !idcheck_state(st))) {
        printf("idcheck: %d frames, %d conflict frames\n", f, ed.idcheck_conflicts);
        fflush(stdout);
        sapp_request_quit();
        return;
    }
    float x = k_idcheck_x[in / per_col];
    if (x < 0.0f) x += sapp_widthf();
    float y = (float)((in % per_col) / 2 * IDCHECK_STEP + 4);
    ImGui::GetIO().AddMousePosEvent(x, y);
}

static void frame(void) {
    simgui_frame_desc_t fd = {};
    fd.width = sapp_width();
    fd.height = sapp_height();
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    if (ed.idcheck) {
        idcheck_before_frame();
    }
    simgui_new_frame(&fd);
    if (ed.idcheck && GImGui->DebugDrawIdConflictsId != 0) {
        ImVec2 m = ImGui::GetIO().MousePos;
        if (ed.idcheck_conflicts < 20) {
            printf("idcheck: ID conflict under the mouse at %.0f,%.0f (selected obj %d, section %d, tile %d)\n", m.x, m.y,
                   ed.selected, ed.section_selected, ed.tile_sel ? 1 : 0);
        }
        ed.idcheck_conflicts++;
    }
    dock_panels();
    handle_input();

    if (!ed.idcheck) {
        draw_map();  // the ID sweep only needs the panels
    }
    ui_toolbar();
    ui_tiles();
    ui_properties();
    ui_script();
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
    if (ed.idcheck) {
        printf("idcheck: %s\n", ed.idcheck_conflicts ? "FAILED" : "PASS no ImGui ID conflicts");
        fflush(stdout);
        exit(ed.idcheck_conflicts ? 1 : 0);
    }
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--idcheck") == 0) {
            // tall, so every row of the docked panels is on screen
            ed.idcheck = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                ed.idcheck_states = atoi(argv[i + 1]);
            }
            desc.width = 1600;
            desc.height = 2400;
        }
    }
    desc.window_title = "sokol-ink-ribbon level editor";
    desc.icon.sokol_default = true;
    desc.logger.func = slog_func;
    return desc;
}
