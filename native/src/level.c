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
        "door", "item", "light", "switch", "typewriter", "file",
        "safe", "obstacle", "movable", "openable", "start", "goal",
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

const char* item_name(item_t item) {
    static const char* names[ITEM_COUNT] = {
        "bottle", "coin", "herb", "ink_ribbon", "screwdriver",
        "injector", "fuse", "lighter", "cherub_key",
    };
    return (item >= 0 && item < ITEM_COUNT) ? names[item] : "herb";
}

item_t item_from_name(const char* name) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (strcmp(name, item_name((item_t)i)) == 0) {
            return (item_t)i;
        }
    }
    return ITEM_HERB;
}

const char* door_state_name(int state) {
    static const char* names[4] = { "unknown", "locked", "unlocked", "unopenable" };
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
            o->id = lv->obj_count + 1;
            int v;
            char kind[LEVEL_MAX_NAME] = "item";
            if ((v = obj_find(js, t, i, "kind")) >= 0) tok_str(js, &t[v], kind, LEVEL_MAX_NAME);
            o->kind = obj_kind_from_name(kind);
            if ((v = obj_find(js, t, i, "id")) >= 0) o->id = tok_int(js, &t[v], o->id);
            if ((v = obj_find(js, t, i, "x")) >= 0) o->x = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "y")) >= 0) o->y = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "horizontal")) >= 0) o->horizontal = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "open")) >= 0) o->open = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "key_id")) >= 0) o->key_id = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "auto_close")) >= 0) o->auto_close = tok_float(js, &t[v], 0.0f);
            if ((v = obj_find(js, t, i, "breakable")) >= 0) o->breakable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "group_id")) >= 0) o->group_id = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "radius")) >= 0) o->radius = tok_float(js, &t[v], 0.0f);
            if ((v = obj_find(js, t, i, "climbable")) >= 0) o->climbable = tok_bool(js, &t[v], false) ? 1 : 0;
            if ((v = obj_find(js, t, i, "code")) >= 0) o->code = tok_int(js, &t[v], 0);
            if ((v = obj_find(js, t, i, "name")) >= 0) tok_str(js, &t[v], o->name, LEVEL_MAX_NAME);
            char state[LEVEL_MAX_NAME] = "unknown";
            if ((v = obj_find(js, t, i, "state")) >= 0) {
                if (t[v].type == JSMN_STRING) {
                    tok_str(js, &t[v], state, LEVEL_MAX_NAME);
                    o->state = door_state_from_name(state);
                } else {
                    o->state = tok_int(js, &t[v], 0);
                }
            }
            char item[LEVEL_MAX_NAME] = "herb";
            if ((v = obj_find(js, t, i, "item")) >= 0) {
                tok_str(js, &t[v], item, LEVEL_MAX_NAME);
                o->item_type = item_from_name(item);
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
    if ((v = obj_find(json, tokens, 0, "walls")) >= 0) parse_walls(json, tokens, lv, v);
    if ((v = obj_find(json, tokens, 0, "sections")) >= 0) parse_sections(json, tokens, lv, v);
    if ((v = obj_find(json, tokens, 0, "objects")) >= 0) parse_objects(json, tokens, lv, v);
    free(tokens);
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

int level_to_json(const level_t* lv, char* out, int cap) {
    int n = 0;
    n = ap(out, cap, n, "{\n  \"name\": \"%s\",\n  \"cols\": %d,\n  \"rows\": %d,\n", lv->name, lv->cols, lv->rows);
    n = ap(out, cap, n, "  \"start\": [%d, %d],\n", lv->start_x, lv->start_y);
    n = ap(out, cap, n, "  \"walls\": [");
    for (int i = 0; i < lv->wall_count; i++) {
        const wall_seg_t* w = &lv->walls[i];
        n = ap(out, cap, n, "%s\n    [%d, %d, %d, %d]", i ? "," : "", w->x0, w->y0, w->x1, w->y1);
    }
    n = ap(out, cap, n, "\n  ],\n  \"sections\": [");
    for (int i = 0; i < lv->section_count; i++) {
        const section_t* s = &lv->sections[i];
        n = ap(out, cap, n, "%s\n    { \"name\": \"%s\", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d }",
               i ? "," : "", s->name, s->x, s->y, s->w, s->h);
    }
    n = ap(out, cap, n, "\n  ],\n  \"objects\": [");
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        n = ap(out, cap, n, "%s\n    { \"kind\": \"%s\", \"id\": %d, \"x\": %d, \"y\": %d",
               i ? "," : "", obj_kind_name(o->kind), o->id, o->x, o->y);
        if (o->kind == OBJ_DOOR) {
            n = ap(out, cap, n, ", \"horizontal\": %d, \"state\": \"%s\", \"open\": %s, \"key_id\": %d, \"auto_close\": %.2f, \"breakable\": %s",
                   o->horizontal, door_state_name(o->state), o->open ? "true" : "false",
                   o->key_id, o->auto_close, o->breakable ? "true" : "false");
        } else if (o->kind == OBJ_ITEM) {
            n = ap(out, cap, n, ", \"item\": \"%s\"", item_name((item_t)o->item_type));
        } else if (o->kind == OBJ_LIGHT) {
            n = ap(out, cap, n, ", \"radius\": %.2f, \"state\": %d, \"group_id\": %d, \"breakable\": %s",
                   o->radius, o->state, o->group_id, o->breakable ? "true" : "false");
        } else if (o->kind == OBJ_SWITCH) {
            n = ap(out, cap, n, ", \"group_id\": %d", o->group_id);
        } else if (o->kind == OBJ_OBSTACLE) {
            n = ap(out, cap, n, ", \"climbable\": %s", o->climbable ? "true" : "false");
        } else if (o->kind == OBJ_FILE || o->kind == OBJ_SAFE) {
            n = ap(out, cap, n, ", \"code\": %d", o->code);
        }
        if (o->name[0]) {
            n = ap(out, cap, n, ", \"name\": \"%s\"", o->name);
        }
        n = ap(out, cap, n, " }");
    }
    n = ap(out, cap, n, "\n  ]\n}\n");
    return n;
}
