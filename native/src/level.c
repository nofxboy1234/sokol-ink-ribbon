#include "level.h"
#include "json/jsmn.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool tok_eq(const char* js, const jsmntok_t* t, const char* s) {
    int len = t->end - t->start;
    return (int)strlen(s) == len && strncmp(js + t->start, s, (size_t)len) == 0;
}

static int tok_int(const char* js, const jsmntok_t* t, int fallback) {
    char buf[32];
    int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof(buf)) {
        return fallback;
    }
    memcpy(buf, js + t->start, (size_t)len);
    buf[len] = 0;
    return atoi(buf);
}

static float tok_float(const char* js, const jsmntok_t* t, float fallback) {
    char buf[32];
    int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof(buf)) {
        return fallback;
    }
    memcpy(buf, js + t->start, (size_t)len);
    buf[len] = 0;
    return (float)atof(buf);
}

static bool tok_bool(const char* js, const jsmntok_t* t, bool fallback) {
    if (t->type != JSMN_PRIMITIVE) {
        return fallback;
    }
    if (tok_eq(js, t, "true")) {
        return true;
    }
    if (tok_eq(js, t, "false")) {
        return false;
    }
    return fallback;
}

static void tok_str(const char* js, const jsmntok_t* t, char* out, int cap) {
    int len = t->end - t->start;
    if (len >= cap) {
        len = cap - 1;
    }
    if (len < 0) {
        len = 0;
    }
    memcpy(out, js + t->start, (size_t)len);
    out[len] = 0;
}

// Number of tokens an object/array/scalar occupies, children included.
static int tok_span(const jsmntok_t* t, int i) {
    if (t[i].type == JSMN_OBJECT) {
        int span = 1;
        int k = i + 1;
        for (int p = 0; p < t[i].size; p++) {
            span += 1; // key token
            int v = k + 1;
            int vs = tok_span(t, v);
            span += vs;
            k = v + vs;
        }
        return span;
    }
    if (t[i].type == JSMN_ARRAY) {
        int span = 1;
        int k = i + 1;
        for (int e = 0; e < t[i].size; e++) {
            int es = tok_span(t, k);
            span += es;
            k += es;
        }
        return span;
    }
    return 1;
}

static int tok_next(const jsmntok_t* t, int i) {
    return i + tok_span(t, i);
}

static int obj_find(const char* js, const jsmntok_t* t, int obj, const char* key) {
    if (t[obj].type != JSMN_OBJECT) {
        return -1;
    }
    int pairs = t[obj].size;
    int i = obj + 1;
    for (int p = 0; p < pairs; p++) {
        int value = i + 1;
        if (t[i].type == JSMN_STRING && tok_eq(js, &t[i], key)) {
            return value;
        }
        i = value + tok_span(t, value);
    }
    return -1;
}

static int arr_int(const char* js, const jsmntok_t* t, int arr, int index, int fallback) {
    if (t[arr].type != JSMN_ARRAY || index < 0 || index >= t[arr].size) {
        return fallback;
    }
    int i = arr + 1;
    for (int n = 0; n < index; n++) {
        i = tok_next(t, i);
    }
    return tok_int(js, &t[i], fallback);
}

void level_init(level_t* lv) {
    memset(lv, 0, sizeof(*lv));
    strcpy(lv->name, "untitled");
    lv->cols = 32;
    lv->rows = 24;
}

const char* obj_kind_name(obj_kind_t kind) {
    static const char* names[OBJ_KIND_COUNT] = {
        "door", "item", "fusebox", "light", "switch", "typewriter", "file",
        "safe", "obstacle", "start", "goal", "recipe", "cabinet", "stairs",
    };
    return (kind >= 0 && kind < OBJ_KIND_COUNT) ? names[kind] : "door";
}

obj_kind_t obj_kind_from_name(const char* name) {
    for (int i = 0; i < OBJ_KIND_COUNT; i++) {
        if (strcmp(name, obj_kind_name((obj_kind_t)i)) == 0) {
            return (obj_kind_t)i;
        }
    }
    return OBJ_ITEM;
}

typedef struct {
    const char* id;
    const char* label;
    float r, g, b;
    int icon;
} item_info_t;

static const item_info_t k_items[ITEM_COUNT] = {
    [ITEM_BOTTLE] = { "bottle", "Empty Bottle", 0.0f, 0.031f, 1.0f, 0 },
    [ITEM_COIN] = { "coin", "Antique Coin", 0.733f, 0.733f, 0.733f, 0 },
    [ITEM_HERB] = { "herb", "Green Herb", 0.0f, 1.0f, 0.149f, 0 },
    [ITEM_INK_RIBBON] = { "ink_ribbon", "Ink Ribbon", 0.8f, 0.0f, 0.4f, 3 },
    [ITEM_SCREWDRIVER] = { "screwdriver", "Screwdriver", 1.0f, 0.0f, 0.933f, 2 },
    [ITEM_INJECTOR] = { "injector", "Med Injector", 0.0f, 0.8f, 0.15f, 3 },
    [ITEM_FUSE] = { "fuse", "Fuse", 0.0f, 1.0f, 0.733f, 0 },
    [ITEM_LIGHTER] = { "lighter", "Lighter", 0.95f, 0.95f, 0.0f, 1 },
    [ITEM_CHERUB_KEY] = { "cherub_key", "Cherub Key", 0.9f, 0.0f, 0.15f, 3 },
    [ITEM_MIXED_HERB] = { "mixed_herb", "Mixed Herb (G+G)", 0.13f, 0.45f, 0.10f, 0 },
};

static const item_info_t* item_row(int item) {
    if (item < 0 || item >= ITEM_COUNT || !k_items[item].id) {
        return 0;
    }
    return &k_items[item];
}

const char* item_name(item_t item) {
    const item_info_t* row = item_row((int)item);
    return row ? row->id : "herb";
}

item_t item_from_name(const char* name) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (strcmp(name, item_name((item_t)i)) == 0) {
            return (item_t)i;
        }
    }
    return ITEM_HERB;
}

const char* item_display_name(int item) {
    const item_info_t* row = item_row(item);
    return (row && row->label) ? row->label : "Item";
}

const char* door_state_name(int state) {
    static const char* names[4] = { "unknown", "locked", "unlocked", "broken" };
    return (state >= 0 && state < 4) ? names[state] : "unknown";
}

int door_state_from_name(const char* name) {
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, door_state_name(i)) == 0) {
            return i;
        }
    }
    return DOOR_UNKNOWN;
}

void item_color_rgb(int item, float* r, float* g, float* b) {
    const item_info_t* row = item_row(item);
    if (!row) {
        *r = 0.118f;
        *g = 0.118f;
        *b = 0.118f;
        return;
    }
    *r = row->r;
    *g = row->g;
    *b = row->b;
}

static int hex_channel(float channel) {
    int value = (int)(channel * 255.0f + 0.5f);
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

const char* item_color_hex(int item) {
    static char bufs[ITEM_COUNT][8];
    const item_info_t* row = item_row(item);
    if (!row) {
        return "#1e1e1e";
    }
    snprintf(
        bufs[item],
        sizeof(bufs[item]),
        "#%02x%02x%02x",
        hex_channel(row->r),
        hex_channel(row->g),
        hex_channel(row->b));
    return bufs[item];
}

int item_icon_shape(int item) {
    const item_info_t* row = item_row(item);
    return row ? row->icon : 0;
}

void door_color_rgb(int state, float* r, float* g, float* b) {
    switch (state) {
        case DOOR_LOCKED: *r = 1.0f; *g = 0.0f; *b = 0.165f; break;
        case DOOR_UNLOCKED: *r = 0.118f; *g = 1.0f; *b = 0.973f; break;
        default: *r = 0.616f; *g = 0.616f; *b = 0.616f; break;
    }
}

const obj_t* level_obj_by_id(const level_t* lv, int id) {
    for (int i = 0; i < lv->obj_count; i++) {
        if (lv->objs[i].id == id) return &lv->objs[i];
    }
    return NULL;
}

const obj_t* level_stairs_at(const level_t* lv, int x, int y) {
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind == OBJ_STAIRS && x >= o->x && x < o->x + o->w && y >= o->y && y < o->y + o->h) return o;
    }
    return NULL;
}

static const char k_tile_glyphs[TILE_KIND_COUNT] = { ' ', '.', 'a', 'b', 'c', 'd', '-', '|' };
static const char* k_tile_names[TILE_KIND_COUNT] = { "void", "floor", "nw", "ne", "sw", "se", "h", "v" };

char tile_glyph(tile_t t) {
    return (t >= 0 && t < TILE_KIND_COUNT) ? k_tile_glyphs[t] : ' ';
}

tile_t tile_from_glyph(char c) {
    for (int i = 0; i < TILE_KIND_COUNT; i++) {
        if (k_tile_glyphs[i] == c) return (tile_t)i;
    }
    return TILE_VOID;
}

const char* tile_name(tile_t t) {
    return (t >= 0 && t < TILE_KIND_COUNT) ? k_tile_names[t] : "void";
}

tile_t tile_from_name(const char* name) {
    for (int i = 0; i < TILE_KIND_COUNT; i++) {
        if (strcmp(name, k_tile_names[i]) == 0) return (tile_t)i;
    }
    return (tile_t)-1;
}

tile_t level_tile(const level_t* lv, int x, int y) {
    if (x < 0 || y < 0 || x >= lv->cols || y >= lv->rows || x >= LEVEL_MAX_COLS || y >= LEVEL_MAX_ROWS) {
        return TILE_VOID;
    }
    return (tile_t)lv->tiles[y * LEVEL_MAX_COLS + x];
}

void level_set_tile(level_t* lv, int x, int y, tile_t t) {
    if (x < 0 || y < 0 || x >= lv->cols || y >= lv->rows || x >= LEVEL_MAX_COLS || y >= LEVEL_MAX_ROWS) {
        return;
    }
    lv->tiles[y * LEVEL_MAX_COLS + x] = (uint8_t)t;
    lv->has_tiles = 1;
}

static bool is_floor(const level_t* lv, int x, int y) {
    return level_tile(lv, x, y) == TILE_FLOOR;
}

static void add_wall(level_t* lv, int x0, int y0, int x1, int y1) {
    if (lv->wall_count < LEVEL_MAX_WALLS) {
        lv->walls[lv->wall_count++] = (wall_seg_t){ x0, y0, x1, y1 };
    }
}

void level_compile_tiles(level_t* lv) {
    lv->wall_count = 0;
    for (int y = 0; y <= lv->rows; y++) {
        int run = -1;
        for (int x = 0; x <= lv->cols; x++) {
            bool edge = x < lv->cols && is_floor(lv, x, y - 1) != is_floor(lv, x, y);
            if (edge && run < 0) run = x;
            if (!edge && run >= 0) {
                add_wall(lv, run, y, x, y);
                run = -1;
            }
        }
    }
    for (int x = 0; x <= lv->cols; x++) {
        int run = -1;
        for (int y = 0; y <= lv->rows; y++) {
            bool edge = y < lv->rows && is_floor(lv, x - 1, y) != is_floor(lv, x, y);
            if (edge && run < 0) run = y;
            if (!edge && run >= 0) {
                add_wall(lv, x, run, x, y);
                run = -1;
            }
        }
    }
}

// One cell of a doorway along axis a in band r: floor, with floor on both
// sides across the band.
static bool fit_floor(const level_t* lv, bool h, int a, int r) {
    return h ? is_floor(lv, a, r) : is_floor(lv, r, a);
}

static bool door_cell(const level_t* lv, bool h, int a, int r) {
    return fit_floor(lv, h, a, r) && fit_floor(lv, h, a, r - 1) && fit_floor(lv, h, a, r + 1);
}

// The doorway through band r that contains a: its first cell and width.
static bool fit_gap(const level_t* lv, bool h, int a, int r, int* lo_out, int* gap_out) {
    if (!door_cell(lv, h, a, r)) return false;
    int lo = a, hi = a;
    while (door_cell(lv, h, lo - 1, r) && a - lo < DOOR_FIT_MAX_GAP) lo--;
    while (door_cell(lv, h, hi + 1, r) && hi - a < DOOR_FIT_MAX_GAP) hi++;
    // both ends must stop at a solid jamb cell in the band itself (a wall
    // gap, or the two walls of a corridor a barrier closes off)
    if (fit_floor(lv, h, lo - 1, r) || fit_floor(lv, h, hi + 1, r)) return false;
    int gap = hi - lo + 1;
    if (gap > DOOR_FIT_MAX_GAP) return false;
    *lo_out = lo;
    *gap_out = gap;
    return true;
}

bool level_door_fit(const level_t* lv, int x, int y, int* ox, int* oy, int* horizontal, int* span) {
    int best_gap = 0, best_off = 0;
    bool found = false;
    for (int hh = 1; hh >= 0; hh--) {
        bool h = hh != 0;
        int a = h ? x : y, r0 = h ? y : x;
        static const int offs[3] = { 0, -1, 1 };
        for (int k = 0; k < 3; k++) {
            int lo, gap;
            if (!fit_gap(lv, h, a, r0 + offs[k], &lo, &gap)) continue;
            int off = offs[k] < 0 ? -offs[k] : offs[k];
            if (!found || gap < best_gap || (gap == best_gap && off < best_off)) {
                found = true;
                best_gap = gap;
                best_off = off;
                *horizontal = hh;
                *span = gap;
                *ox = h ? lo : r0 + offs[k];
                *oy = h ? r0 + offs[k] : lo;
            }
            break;
        }
    }
    return found;
}

bool level_door_flush(const level_t* lv, const obj_t* d) {
    int span = d->span > 0 ? d->span : 1;
    // fit from every cell of the door: each must land on this exact door
    for (int k = 0; k < span; k++) {
        int cx = d->horizontal ? d->x + k : d->x;
        int cy = d->horizontal ? d->y : d->y + k;
        int fx, fy, fh, fs;
        if (!level_door_fit(lv, cx, cy, &fx, &fy, &fh, &fs)) return false;
        if (fx != d->x || fy != d->y || fh != (d->horizontal ? 1 : 0) || fs != span) return false;
    }
    return true;
}

static void parse_tiles(const char* js, const jsmntok_t* t, level_t* lv, int arr) {
    if (t[arr].type != JSMN_ARRAY) {
        return;
    }
    int i = arr + 1;
    for (int y = 0; y < t[arr].size; y++) {
        if (t[i].type == JSMN_STRING) {
            for (int x = 0; x < t[i].end - t[i].start; x++) {
                level_set_tile(lv, x, y, tile_from_glyph(js[t[i].start + x]));
            }
        }
        i = tok_next(t, i);
    }
    lv->has_tiles = 1;
}

static void parse_walls(const char* js, const jsmntok_t* t, level_t* lv, int arr) {
    if (t[arr].type != JSMN_ARRAY) {
        return;
    }
    int count = t[arr].size;
    int i = arr + 1;
    for (int e = 0; e < count && lv->wall_count < LEVEL_MAX_WALLS; e++) {
        if (t[i].type == JSMN_ARRAY && t[i].size >= 4) {
            wall_seg_t* w = &lv->walls[lv->wall_count++];
            w->x0 = arr_int(js, t, i, 0, 0);
            w->y0 = arr_int(js, t, i, 1, 0);
            w->x1 = arr_int(js, t, i, 2, 0);
            w->y1 = arr_int(js, t, i, 3, 0);
        }
        i = tok_next(t, i);
    }
}

static void parse_sections(const char* js, const jsmntok_t* t, level_t* lv, int arr) {
    if (t[arr].type != JSMN_ARRAY) {
        return;
    }
    int count = t[arr].size;
    int i = arr + 1;
    for (int e = 0; e < count && lv->section_count < LEVEL_MAX_SECTIONS; e++) {
        if (t[i].type == JSMN_OBJECT) {
            section_t* s = &lv->sections[lv->section_count++];
            memset(s, 0, sizeof(*s));
            int v;
            if ((v = obj_find(js, t, i, "name")) >= 0) tok_str(js, &t[v], s->name, LEVEL_MAX_NAME);
            s->x = (v = obj_find(js, t, i, "x")) >= 0 ? tok_int(js, &t[v], 0) : 0;
            s->y = (v = obj_find(js, t, i, "y")) >= 0 ? tok_int(js, &t[v], 0) : 0;
            s->w = (v = obj_find(js, t, i, "w")) >= 0 ? tok_int(js, &t[v], 1) : 1;
            s->h = (v = obj_find(js, t, i, "h")) >= 0 ? tok_int(js, &t[v], 1) : 1;
        }
        i = tok_next(t, i);
    }
}

static void parse_objects(const char* js, const jsmntok_t* t, level_t* lv, int arr) {
    if (t[arr].type != JSMN_ARRAY) {
        return;
    }
    int count = t[arr].size;
    int i = arr + 1;
    for (int e = 0; e < count && lv->obj_count < LEVEL_MAX_OBJECTS; e++) {
        if (t[i].type == JSMN_OBJECT) {
            obj_t* o = &lv->objs[lv->obj_count];
            memset(o, 0, sizeof(*o));
            o->radius = 0.0f;
            o->discover_state = -1;
            o->id = lv->obj_count + 1;
            int v;
            char kind[LEVEL_MAX_NAME] = "item";
            if ((v = obj_find(js, t, i, "kind")) >= 0) tok_str(js, &t[v], kind, LEVEL_MAX_NAME);
            o->kind = obj_kind_from_name(kind);
            // legacy kinds: movable/openable are obstacle properties now
            if (strcmp(kind, "movable") == 0) { o->kind = OBJ_OBSTACLE; o->movable = 1; }
            if (strcmp(kind, "openable") == 0) { o->kind = OBJ_OBSTACLE; o->openable = 1; }
            if ((v = obj_find(js, t, i, "id")) >= 0) o->id = tok_int(js, &t[v], o->id);
            if ((v = obj_find(js, t, i, "x")) >= 0) o->x = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "y")) >= 0) o->y = tok_int(js, &t[v], 0);
            o->w = (v = obj_find(js, t, i, "w")) >= 0 ? tok_int(js, &t[v], 1) : 1;
            o->h = (v = obj_find(js, t, i, "h")) >= 0 ? tok_int(js, &t[v], 1) : 1;
            if (o->w < 1) o->w = 1;
            if (o->h < 1) o->h = 1;
            if ((v = obj_find(js, t, i, "horizontal")) >= 0) o->horizontal = tok_int(js, &t[v], 0);
            o->span = (v = obj_find(js, t, i, "span")) >= 0 ? tok_int(js, &t[v], 1) : 1;
            if (o->span < 1) o->span = 1;
            if ((v = obj_find(js, t, i, "open")) >= 0) o->open = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "key_id")) >= 0) o->key_id = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "link")) >= 0) o->link_id = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "auto_close")) >= 0) o->auto_close = tok_float(js, &t[v], 0.0f);
            if ((v = obj_find(js, t, i, "breakable")) >= 0) o->breakable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "group_id")) >= 0) o->group_id = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "radius")) >= 0) o->radius = tok_float(js, &t[v], 0.0f);
            if ((v = obj_find(js, t, i, "climbable")) >= 0) o->climbable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "movable")) >= 0) o->movable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "openable")) >= 0) o->openable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "code")) >= 0) o->code = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "name")) >= 0) tok_str(js, &t[v], o->name, LEVEL_MAX_NAME);
            char state[LEVEL_MAX_NAME] = "unknown";
            if ((v = obj_find(js, t, i, "state")) >= 0) {
                if (t[v].type == JSMN_STRING && o->kind == OBJ_LIGHT) {
                    o->state = LIGHT_OFF;
                } else if (t[v].type == JSMN_STRING) {
                    tok_str(js, &t[v], state, LEVEL_MAX_NAME);
                    o->state = door_state_from_name(state);
                } else {
                    o->state = tok_int(js, &t[v], 0);
                }
            }
            if (o->kind == OBJ_LIGHT && (o->state < LIGHT_OFF || o->state > LIGHT_BROKEN)) {
                o->state = LIGHT_OFF;
            }
            char item[LEVEL_MAX_NAME] = "herb";
            if ((v = obj_find(js, t, i, "item")) >= 0) {
                tok_str(js, &t[v], item, LEVEL_MAX_NAME);
                o->item_type = item_from_name(item);
            }
            if ((v = obj_find(js, t, i, "discover")) >= 0) {
                char discover[LEVEL_MAX_NAME] = "";
                tok_str(js, &t[v], discover, LEVEL_MAX_NAME);
                o->discover_state = door_state_from_name(discover);
            }
            lv->obj_count++;
        }
        i = tok_next(t, i);
    }
}

bool level_from_json(level_t* lv, const char* json) {
    level_init(lv);
    jsmn_parser parser;
    jsmn_init(&parser);
    int len = (int)strlen(json);
    int count = jsmn_parse(&parser, json, (size_t)len, NULL, 0);
    if (count <= 0) {
        return false;
    }
    jsmntok_t* tokens = (jsmntok_t*)malloc(sizeof(jsmntok_t) * (size_t)count);
    if (!tokens) {
        return false;
    }
    jsmn_init(&parser);
    count = jsmn_parse(&parser, json, (size_t)len, tokens, (unsigned)count);
    if (count < 0 || tokens[0].type != JSMN_OBJECT) {
        free(tokens);
        return false;
    }
    int v;
    if ((v = obj_find(json, tokens, 0, "name")) >= 0) tok_str(json, &tokens[v], lv->name, LEVEL_MAX_NAME);
    if ((v = obj_find(json, tokens, 0, "cols")) >= 0) lv->cols = tok_int(json, &tokens[v], lv->cols);
    if ((v = obj_find(json, tokens, 0, "rows")) >= 0) lv->rows = tok_int(json, &tokens[v], lv->rows);
    if ((v = obj_find(json, tokens, 0, "start")) >= 0) {
        lv->start_x = arr_int(json, tokens, v, 0, 0);
        lv->start_y = arr_int(json, tokens, v, 1, 0);
    }
    if ((v = obj_find(json, tokens, 0, "tiles")) >= 0) {
        parse_tiles(json, tokens, lv, v);
    } else if ((v = obj_find(json, tokens, 0, "walls")) >= 0) {
        parse_walls(json, tokens, lv, v);
    }
    if ((v = obj_find(json, tokens, 0, "sections")) >= 0) parse_sections(json, tokens, lv, v);
    if ((v = obj_find(json, tokens, 0, "objects")) >= 0) parse_objects(json, tokens, lv, v);
    free(tokens);
    if (lv->has_tiles) {
        level_compile_tiles(lv);
    }

    // Give every object a unique id and remap link references so links always
    // resolve to the object they were made against.
    int remap[LEVEL_MAX_OBJECTS + 1];
    for (int i = 0; i <= LEVEL_MAX_OBJECTS; i++) {
        remap[i] = 0;
    }
    for (int i = 0; i < lv->obj_count; i++) {
        int old = lv->objs[i].id;
        if (old >= 0 && old <= LEVEL_MAX_OBJECTS && remap[old] == 0) {
            remap[old] = i + 1;
        }
    }
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (o->link_id > 0) {
            o->link_id = (o->link_id <= LEVEL_MAX_OBJECTS) ? remap[o->link_id] : 0;
        }
        o->id = i + 1;
    }
    return true;
}

static int ap(char* out, int cap, int n, const char* fmt, ...) {
    if (n < 0) {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    int wrote = vsnprintf(out + n, (size_t)(cap - n), fmt, args);
    va_end(args);
    if (wrote < 0 || wrote >= cap - n) {
        return -1;
    }
    return n + wrote;
}

// Append a JSON string literal with the value escaped.
static int ap_str(char* out, int cap, int n, const char* s) {
    n = ap(out, cap, n, "\"");
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            n = ap(out, cap, n, "\\%c", c);
        } else if (c == '\n') {
            n = ap(out, cap, n, "\\n");
        } else if (c == '\r') {
            n = ap(out, cap, n, "\\r");
        } else if (c == '\t') {
            n = ap(out, cap, n, "\\t");
        } else if (c < 0x20) {
            n = ap(out, cap, n, "\\u%04x", c);
        } else {
            n = ap(out, cap, n, "%c", c);
        }
        if (n < 0) {
            return n;
        }
    }
    return ap(out, cap, n, "\"");
}

int level_to_json(const level_t* lv, char* out, int cap) {
    int n = 0;
    n = ap(out, cap, n, "{\n  \"name\": ");
    n = ap_str(out, cap, n, lv->name);
    n = ap(out, cap, n, ",\n  \"cols\": %d,\n  \"rows\": %d,\n", lv->cols, lv->rows);
    n = ap(out, cap, n, "  \"start\": [%d, %d],\n", lv->start_x, lv->start_y);
    if (lv->has_tiles) {
        n = ap(out, cap, n, "  \"tiles\": [");
        for (int y = 0; y < lv->rows; y++) {
            n = ap(out, cap, n, "%s\n    \"", y ? "," : "");
            for (int x = 0; x < lv->cols; x++) {
                n = ap(out, cap, n, "%c", tile_glyph(level_tile(lv, x, y)));
            }
            n = ap(out, cap, n, "\"");
        }
    } else {
        n = ap(out, cap, n, "  \"walls\": [");
        for (int i = 0; i < lv->wall_count; i++) {
            const wall_seg_t* w = &lv->walls[i];
            n = ap(out, cap, n, "%s\n    [%d, %d, %d, %d]", i ? "," : "", w->x0, w->y0, w->x1, w->y1);
        }
    }
    n = ap(out, cap, n, "\n  ],\n  \"sections\": [");
    for (int i = 0; i < lv->section_count; i++) {
        const section_t* s = &lv->sections[i];
        n = ap(out, cap, n, "%s\n    { \"name\": ", i ? "," : "");
        n = ap_str(out, cap, n, s->name);
        n = ap(out, cap, n, ", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d }", s->x, s->y, s->w, s->h);
    }
    n = ap(out, cap, n, "\n  ],\n  \"objects\": [");
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        n = ap(out, cap, n, "%s\n    { \"kind\": \"%s\", \"id\": %d, \"x\": %d, \"y\": %d",
               i ? "," : "", obj_kind_name(o->kind), o->id, o->x, o->y);
        if (o->kind == OBJ_DOOR) {
            n = ap(out, cap, n, ", \"horizontal\": %d, \"span\": %d, \"state\": \"%s\"",
                   o->horizontal, o->span, door_state_name(o->state));
            if (o->discover_state >= 0) {
                n = ap(out, cap, n, ", \"discover\": \"%s\"", door_state_name(o->discover_state));
            }
            n = ap(out, cap, n, ", \"open\": %s, \"key_id\": %d, \"auto_close\": %.2f, \"breakable\": %s",
                   o->open ? "true" : "false", o->key_id, o->auto_close, o->breakable ? "true" : "false");
        } else if (o->kind == OBJ_ITEM) {
            n = ap(out, cap, n, ", \"item\": \"%s\"", item_name((item_t)o->item_type));
        } else if (o->kind == OBJ_LIGHT) {
            n = ap(out, cap, n, ", \"radius\": %.2f, \"state\": %d, \"group_id\": %d, \"breakable\": %s",
                   o->radius, o->state, o->group_id, o->breakable ? "true" : "false");
        } else if (o->kind == OBJ_SWITCH) {
            n = ap(out, cap, n, ", \"group_id\": %d", o->group_id);
        } else if (o->kind == OBJ_STAIRS) {
            n = ap(out, cap, n, ", \"w\": %d, \"h\": %d", o->w, o->h);
        } else if (o->kind == OBJ_OBSTACLE) {
            n = ap(out, cap, n, ", \"w\": %d, \"h\": %d, \"climbable\": %s",
                   o->w, o->h, o->climbable ? "true" : "false");
            if (o->movable) n = ap(out, cap, n, ", \"movable\": true");
            if (o->openable) n = ap(out, cap, n, ", \"openable\": true, \"open\": %s", o->open ? "true" : "false");
        } else if (o->kind == OBJ_FILE || o->kind == OBJ_SAFE) {
            n = ap(out, cap, n, ", \"code\": %d", o->code);
        }
        if (o->link_id > 0) {
            n = ap(out, cap, n, ", \"link\": %d", o->link_id);
        }
        if (o->name[0]) {
            n = ap(out, cap, n, ", \"name\": ");
            n = ap_str(out, cap, n, o->name);
        }
        n = ap(out, cap, n, " }");
    }
    n = ap(out, cap, n, "\n  ]\n}\n");
    return n;
}
