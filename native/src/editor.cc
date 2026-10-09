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

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    TOOL_SELECT = 0,
    TOOL_WALL,
    TOOL_PLACE,
};

static struct {
    sg_pass_action pass_action;
    level_t level;
    char path[512];
    int tool;
    int place_kind;
    int place_item;
    int selected;
    int section_selected;
    int drag_obj;
    bool wall_active;
    float wall_x, wall_y;
    float cam_x, cam_y, scale;
    bool panning;
    float pan_x, pan_y;
    bool dirty;
    char status[640];
} ed;

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
    FILE* f = fopen(ed.path, "wb");
    if (!f) {
        snprintf(ed.status, sizeof(ed.status), "cannot write %s", ed.path);
        return;
    }
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    ed.dirty = false;
    snprintf(ed.status, sizeof(ed.status), "saved %s (%d bytes)", ed.path, n);
}

static void add_object(int kind, int x, int y) {
    if (ed.level.obj_count >= LEVEL_MAX_OBJECTS) {
        return;
    }
    obj_t* o = &ed.level.objs[ed.level.obj_count++];
    memset(o, 0, sizeof(*o));
    o->id = ed.level.obj_count;
    o->kind = (obj_kind_t)kind;
    o->x = x;
    o->y = y;
    o->item_type = ed.place_item;
    o->radius = 4.0f;
    o->state = (kind == OBJ_LIGHT) ? 1 : 0;
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

static void pick_object(float wx, float wy) {
    ed.selected = -1;
    float best = 1.0f;
    for (int i = 0; i < ed.level.obj_count; i++) {
        float dx = (float)ed.level.objs[i].x + 0.5f - wx;
        float dy = (float)ed.level.objs[i].y + 0.5f - wy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best) {
            best = d;
            ed.selected = i;
        }
    }
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
    ed.wall_x = (float)px;
    ed.wall_y = (float)py;
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
            fill_rect(sx, sy, ed.scale, ed.scale, 0.06f, 0.09f, 0.16f, 1.0f);
        }
    }
    for (int i = 0; i < ed.level.section_count; i++) {
        section_t* s = &ed.level.sections[i];
        fill_rect(view_x((float)s->x), view_y((float)s->y), s->w * ed.scale, s->h * ed.scale,
                  0.15f, 0.3f, 0.5f, 0.25f);
        if (i == ed.section_selected) {
            draw_line(view_x((float)s->x), view_y((float)s->y), view_x((float)(s->x + s->w)), view_y((float)s->y), 2.0f, 1, 1, 0, 1);
            draw_line(view_x((float)(s->x + s->w)), view_y((float)s->y), view_x((float)(s->x + s->w)), view_y((float)(s->y + s->h)), 2.0f, 1, 1, 0, 1);
            draw_line(view_x((float)(s->x + s->w)), view_y((float)(s->y + s->h)), view_x((float)s->x), view_y((float)(s->y + s->h)), 2.0f, 1, 1, 0, 1);
            draw_line(view_x((float)s->x), view_y((float)(s->y + s->h)), view_x((float)s->x), view_y((float)s->y), 2.0f, 1, 1, 0, 1);
        }
    }
    for (int i = 0; i < ed.level.wall_count; i++) {
        wall_seg_t* w = &ed.level.walls[i];
        draw_line(view_x((float)w->x0), view_y((float)w->y0), view_x((float)w->x1), view_y((float)w->y1),
                  3.0f, 0.5f, 0.75f, 1.0f, 0.95f);
    }
    for (int i = 0; i < ed.level.obj_count; i++) {
        obj_t* o = &ed.level.objs[i];
        float cx = view_x((float)o->x + 0.5f);
        float cy = view_y((float)o->y + 0.5f);
        float scale = ed.scale;
        if (o->kind == OBJ_ITEM) {
            float r, g, b;
            item_color_rgb(o->item_type, &r, &g, &b);
            fill_circle(cx, cy, scale * 0.24f, 0.118f, 0.118f, 0.118f, 1.0f);
            fill_circle(cx, cy, scale * 0.22f, r, g, b, 1.0f);
        } else if (o->kind == OBJ_DOOR) {
            float r, g, b;
            door_color_rgb(o->state, &r, &g, &b);
            float span = scale * (o->span > 0 ? o->span : 1);
            if (o->horizontal) {
                fill_rect(cx - scale * 0.5f, cy - scale * 0.09f, span, scale * 0.18f, r, g, b, 1.0f);
            } else {
                fill_rect(cx - scale * 0.09f, cy - scale * 0.5f, scale * 0.18f, span, r, g, b, 1.0f);
            }
        } else {
            float r = 0.6f, g = 0.6f, b = 0.6f;
            switch (o->kind) {
                case OBJ_LIGHT: r = 1.0f; g = 0.85f; b = 0.3f; break;
                case OBJ_SWITCH: r = 0.95f; g = 1.0f; b = 0.0f; break;
                case OBJ_TYPEWRITER: r = 0.118f; g = 0.118f; b = 0.118f; break;
                case OBJ_FILE: r = 0.95f; g = 0.95f; b = 0.9f; break;
                case OBJ_SAFE: r = 0.616f; g = 0.616f; b = 0.616f; break;
                case OBJ_OBSTACLE: r = 0.5f; g = 0.45f; b = 0.4f; break;
                case OBJ_MOVABLE: r = 0.6f; g = 0.5f; b = 0.3f; break;
                case OBJ_OPENABLE: r = 0.0f; g = 1.0f; b = 0.733f; break;
                case OBJ_FUSEBOX: r = 0.95f; g = 0.85f; b = 0.0f; break;
                case OBJ_START: r = 0.588f; g = 0.118f; b = 1.0f; break;
                case OBJ_GOAL: r = 1.0f; g = 0.0f; b = 0.416f; break;
                default: break;
            }
            float size = scale * 0.3f;
            fill_rect(cx - size, cy - size, size * 2, size * 2, r, g, b, 1.0f);
        }
        if (i == ed.selected) {
            float size = scale * 0.45f;
            draw_line(cx - size, cy - size, cx + size, cy - size, 2.0f, 1, 1, 1, 1);
            draw_line(cx + size, cy - size, cx + size, cy + size, 2.0f, 1, 1, 1, 1);
            draw_line(cx + size, cy + size, cx - size, cy + size, 2.0f, 1, 1, 1, 1);
            draw_line(cx - size, cy + size, cx - size, cy - size, 2.0f, 1, 1, 1, 1);
        }
    }
    if (ed.wall_active) {
        float sx = view_x(ed.wall_x);
        float sy = view_y(ed.wall_y);
        fill_rect(sx - 4, sy - 4, 8, 8, 1, 1, 0, 1);
    }
}

static void ui_toolbar(void) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 260), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools");
    if (ImGui::Button("Save")) {
        save_level();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        load_level();
    }
    ImGui::Separator();
    ImGui::Text("Tool");
    ImGui::RadioButton("Select", &ed.tool, TOOL_SELECT);
    ImGui::RadioButton("Wall line", &ed.tool, TOOL_WALL);
    ImGui::RadioButton("Place", &ed.tool, TOOL_PLACE);
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
    for (int i = 0; i < ed.level.section_count; i++) {
        if (ImGui::Selectable(ed.level.sections[i].name, i == ed.section_selected)) {
            ed.section_selected = i;
        }
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
            const char* states[4] = { "unknown", "locked", "unlocked", "unopenable" };
            changed |= ImGui::Combo("state", &o->state, states, 4);
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
            changed |= ImGui::InputFloat("radius", &o->radius);
            changed |= ImGui::Checkbox("on", (bool*)&o->state);
            changed |= ImGui::InputInt("group_id", &o->group_id);
            changed |= ImGui::Checkbox("breakable", (bool*)&o->breakable);
        } else if (o->kind == OBJ_SWITCH) {
            changed |= ImGui::InputInt("group_id", &o->group_id);
        } else if (o->kind == OBJ_OBSTACLE) {
            changed |= ImGui::Checkbox("climbable", (bool*)&o->climbable);
        } else if (o->kind == OBJ_FILE || o->kind == OBJ_SAFE) {
            changed |= ImGui::InputInt("code", &o->code);
        }
        ed.dirty |= changed;
        if (ImGui::Button("Delete")) {
            delete_selected();
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
        ImGui::Text("Start");
        bool changed = false;
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
                ed.drag_obj = ed.selected;
            } else if (ed.tool == TOOL_PLACE) {
                add_object(ed.place_kind, (int)floorf(wx), (int)floorf(wy));
            }
        }
        // dragging a selected object moves it to the cell under the cursor
        if (ed.drag_obj >= 0 && ed.drag_obj < ed.level.obj_count) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                obj_t* o = &ed.level.objs[ed.drag_obj];
                int nx = (int)floorf(wx);
                int ny = (int)floorf(wy);
                if (nx != o->x || ny != o->y) {
                    o->x = nx;
                    o->y = ny;
                    ed.dirty = true;
                }
            } else {
                ed.drag_obj = -1;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ed.wall_active = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            delete_selected();
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

    ed.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ed.pass_action.colors[0].clear_value = { 0.02f, 0.03f, 0.06f, 1.0f };
    ed.tool = TOOL_SELECT;
    ed.place_kind = OBJ_DOOR;
    ed.place_item = ITEM_HERB;
    ed.selected = -1;
    ed.section_selected = -1;
    ed.drag_obj = -1;
    ed.wall_active = false;
    ed.cam_x = 32.0f;
    ed.cam_y = 24.0f;
    ed.scale = 16.0f;
    ed.dirty = false;
    snprintf(ed.path, sizeof(ed.path), "src/level_01.json");
    load_level();
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
    simgui_shutdown();
    sgl_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
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
