#include "levelscript.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 24
#define MAX_LINE 512

typedef struct {
    level_t* lv;
    int ox, oy;
    char* err;
    int errcap;
    int line;
} ctx_t;

static int fail(ctx_t* c, const char* fmt, ...) {
    int n = snprintf(c->err, (size_t)c->errcap, "line %d: ", c->line);
    va_list args;
    va_start(args, fmt);
    if (n >= 0 && n < c->errcap) vsnprintf(c->err + n, (size_t)(c->errcap - n), fmt, args);
    va_end(args);
    return -1;
}

static bool parse_int(const char* s, int* out) {
    char* end;
    long v = strtol(s, &end, 10);
    if (end == s || *end) return false;
    *out = (int)v;
    return true;
}

static int need_ints(ctx_t* c, char** argv, int first, int count, int* out) {
    for (int i = 0; i < count; i++) {
        if (!parse_int(argv[first + i], &out[i])) return fail(c, "'%s' is not a number", argv[first + i]);
    }
    return 0;
}

static void order(int* a, int* b) {
    if (*a > *b) {
        int t = *a;
        *a = *b;
        *b = t;
    }
}

static void fill(level_t* lv, int x0, int y0, int x1, int y1, tile_t t) {
    order(&x0, &x1);
    order(&y0, &y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) level_set_tile(lv, x, y, t);
}

static void join_name(char** argv, int first, int argc, char* out) {
    out[0] = 0;
    for (int i = first; i < argc; i++) {
        if (i > first) strncat(out, " ", LEVEL_MAX_NAME - strlen(out) - 1);
        strncat(out, argv[i], LEVEL_MAX_NAME - strlen(out) - 1);
    }
}

static void unscore(char* s) {
    for (; *s; s++)
        if (*s == '_') *s = ' ';
}

static obj_t* find_named(level_t* lv, const char* raw) {
    char name[LEVEL_MAX_NAME];
    snprintf(name, sizeof(name), "%s", raw);
    unscore(name);
    for (int i = 0; i < lv->obj_count; i++) {
        if (strcmp(lv->objs[i].name, name) == 0) return &lv->objs[i];
    }
    return NULL;
}

static obj_t* new_obj(ctx_t* c, obj_kind_t kind, int x, int y) {
    level_t* lv = c->lv;
    if (lv->obj_count >= LEVEL_MAX_OBJECTS) return NULL;
    int id = 0;
    for (int i = 0; i < lv->obj_count; i++)
        if (lv->objs[i].id > id) id = lv->objs[i].id;
    obj_t* o = &lv->objs[lv->obj_count++];
    memset(o, 0, sizeof(*o));
    o->id = id + 1;
    o->kind = kind;
    o->x = x;
    o->y = y;
    o->w = o->h = o->span = 1;
    o->discover_state = -1;
    return o;
}

// key=value properties shared by obj, door and item
static int apply_props(ctx_t* c, obj_t* o, char** argv, int first, int argc) {
    for (int i = first; i < argc; i++) {
        char* eq = strchr(argv[i], '=');
        if (!eq) return fail(c, "expected key=value, got '%s'", argv[i]);
        *eq = 0;
        const char* k = argv[i];
        const char* v = eq + 1;
        int n = 0;
        bool num = parse_int(v, &n);
        if (strcmp(k, "name") == 0) {
            snprintf(o->name, LEVEL_MAX_NAME, "%s", v);
            unscore(o->name);
        } else if (strcmp(k, "state") == 0) {
            o->state = num ? n : door_state_from_name(v);
        } else if (strcmp(k, "item") == 0) {
            o->item_type = item_from_name(v);
        } else if (strcmp(k, "key") == 0 || strcmp(k, "key2") == 0 || strcmp(k, "key3") == 0) {
            // key=ITEM: the item that opens this door (implies lock=key)
            int it = item_from_name(v);
            if (strcmp(item_name((item_t)it), v) != 0) return fail(c, "%s: unknown item '%s'", k, v);
            if (k[3] == '2') o->key2_id = it;
            else if (k[3] == '3') o->key3_id = it;
            else o->key_id = it;
            if (o->lock == LOCK_NONE) o->lock = LOCK_KEY;
            o->state = DOOR_LOCKED;
        } else if (strcmp(k, "lock") == 0) {
            int l = door_lock_from_name(v);
            if (l < 0) return fail(c, "lock must be none, key, other_side or jammed, got '%s'", v);
            o->lock = l;
            o->state = l == LOCK_NONE ? DOOR_UNLOCKED : DOOR_LOCKED;
            if (l == LOCK_NONE) o->key_id = o->key2_id = o->key3_id = 0;
        } else if (strcmp(k, "link") == 0) {
            const obj_t* t = find_named(c->lv, v);
            if (!t) return fail(c, "link: no object named '%s'", v);
            o->link_id = t->id;
        } else if (strcmp(k, "radius") == 0) {
            o->radius = (float)atof(v);
        } else if (strcmp(k, "auto_close") == 0) {
            o->auto_close = (float)atof(v);
        } else if (!num) {
            return fail(c, "%s needs a number", k);
        } else if (strcmp(k, "w") == 0) o->w = n;
        else if (strcmp(k, "h") == 0) o->h = n;
        else if (strcmp(k, "span") == 0) o->span = n;
        else if (strcmp(k, "horizontal") == 0) o->horizontal = n;
        else if (strcmp(k, "open") == 0) o->open = n;
        else if (strcmp(k, "key_id") == 0) o->key_id = n;
        else if (strcmp(k, "breakable") == 0) o->breakable = n;
        else if (strcmp(k, "group_id") == 0) o->group_id = n;
        else if (strcmp(k, "climbable") == 0) o->climbable = n;
        else if (strcmp(k, "movable") == 0) o->movable = n;
        else if (strcmp(k, "openable") == 0) o->openable = n;
        else if (strcmp(k, "code") == 0) o->code = n;
        else if (strcmp(k, "qty") == 0) o->qty = n;
        else if (strcmp(k, "side") == 0) o->side = n;
        else return fail(c, "unknown property '%s'", k);
    }
    return 0;
}

// The wall glyph a solid cell should show, from which neighbours are floor
// or wall: a corner where two wall runs meet, else a straight run.
static tile_t wall_shape(const level_t* lv, int x, int y) {
    tile_t t = level_tile(lv, x, y);
    bool l = level_tile(lv, x - 1, y) >= TILE_CORNER_NW, r = level_tile(lv, x + 1, y) >= TILE_CORNER_NW;
    bool u = level_tile(lv, x, y - 1) >= TILE_CORNER_NW, d = level_tile(lv, x, y + 1) >= TILE_CORNER_NW;
    if (r && d && !l && !u) return TILE_CORNER_NW;
    if (l && d && !r && !u) return TILE_CORNER_NE;
    if (r && u && !l && !d) return TILE_CORNER_SW;
    if (l && u && !r && !d) return TILE_CORNER_SE;
    if ((l || r) && !(u || d)) return TILE_WALL_H;
    if ((u || d) && !(l || r)) return TILE_WALL_V;
    return t;
}

// Wrap every floor cell in wall tiles (8-neighbourhood), then pick each
// wall's glyph from its neighbours.
static void autowall(level_t* lv) {
    for (int y = 0; y < lv->rows; y++)
        for (int x = 0; x < lv->cols; x++) {
            if (level_tile(lv, x, y) != TILE_VOID) continue;
            bool touch = false;
            for (int dy = -1; dy <= 1 && !touch; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (level_tile(lv, x + dx, y + dy) == TILE_FLOOR) touch = true;
            if (touch) level_set_tile(lv, x, y, TILE_WALL_H);
        }
    static uint8_t next[LEVEL_MAX_ROWS * LEVEL_MAX_COLS];
    memcpy(next, lv->tiles, sizeof(next));
    for (int y = 0; y < lv->rows; y++)
        for (int x = 0; x < lv->cols; x++)
            if (level_tile(lv, x, y) >= TILE_CORNER_NW) next[y * LEVEL_MAX_COLS + x] = (uint8_t)wall_shape(lv, x, y);
    memcpy(lv->tiles, next, sizeof(next));
}

static int run_line(ctx_t* c, int argc, char** argv) {
    level_t* lv = c->lv;
    const char* cmd = argv[0];
    int a[6];
    int ox = c->ox, oy = c->oy;
    if (strcmp(cmd, "size") == 0) {
        if (argc != 3 || need_ints(c, argv, 1, 2, a)) return argc != 3 ? fail(c, "size COLS ROWS") : -1;
        if (a[0] < 1 || a[1] < 1 || a[0] > LEVEL_MAX_COLS || a[1] > LEVEL_MAX_ROWS)
            return fail(c, "size must be 1..%d x 1..%d", LEVEL_MAX_COLS, LEVEL_MAX_ROWS);
        lv->cols = a[0];
        lv->rows = a[1];
        lv->has_tiles = 1;
    } else if (strcmp(cmd, "clear") == 0) {
        char name[LEVEL_MAX_NAME];
        memcpy(name, lv->name, sizeof(name));
        int cols = lv->cols, rows = lv->rows;
        level_init(lv);
        memcpy(lv->name, name, sizeof(name));
        lv->cols = cols;
        lv->rows = rows;
        lv->has_tiles = 1;
    } else if (strcmp(cmd, "name") == 0) {
        if (argc < 2) return fail(c, "name TEXT");
        join_name(argv, 1, argc, lv->name);
    } else if (strcmp(cmd, "origin") == 0) {
        if (argc != 3 || need_ints(c, argv, 1, 2, a)) return argc != 3 ? fail(c, "origin X Y") : -1;
        c->ox = a[0];
        c->oy = a[1];
    } else if (strcmp(cmd, "tile") == 0) {
        if (argc != 4 && argc != 6) return fail(c, "tile KIND X Y [X2 Y2]");
        tile_t t = tile_from_name(argv[1]);
        if ((int)t < 0) return fail(c, "unknown tile '%s' (void floor nw ne sw se h v)", argv[1]);
        if (need_ints(c, argv, 2, argc - 2, a)) return -1;
        if (argc == 4) { a[2] = a[0]; a[3] = a[1]; }
        fill(lv, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, t);
    } else if (strcmp(cmd, "floor") == 0 || strcmp(cmd, "void") == 0) {
        if (argc != 5 || need_ints(c, argv, 1, 4, a)) return argc != 5 ? fail(c, "%s X0 Y0 X1 Y1", cmd) : -1;
        fill(lv, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, cmd[0] == 'f' ? TILE_FLOOR : TILE_VOID);
    } else if (strcmp(cmd, "hwall") == 0) {
        if (argc != 4 || need_ints(c, argv, 1, 3, a)) return argc != 4 ? fail(c, "hwall X0 X1 Y") : -1;
        fill(lv, a[0] + ox, a[2] + oy, a[1] + ox, a[2] + oy, TILE_WALL_H);
    } else if (strcmp(cmd, "vwall") == 0) {
        if (argc != 4 || need_ints(c, argv, 1, 3, a)) return argc != 4 ? fail(c, "vwall X Y0 Y1") : -1;
        fill(lv, a[0] + ox, a[1] + oy, a[0] + ox, a[2] + oy, TILE_WALL_V);
    } else if (strcmp(cmd, "room") == 0) {
        if (argc < 5 || need_ints(c, argv, 1, 4, a)) return argc < 5 ? fail(c, "room X0 Y0 X1 Y1 [NAME]") : -1;
        int x0 = a[0] + ox, y0 = a[1] + oy, x1 = a[2] + ox, y1 = a[3] + oy;
        order(&x0, &x1);
        order(&y0, &y1);
        if (x1 - x0 < 2 || y1 - y0 < 2) return fail(c, "a room needs at least one floor cell inside its walls");
        fill(lv, x0 + 1, y0 + 1, x1 - 1, y1 - 1, TILE_FLOOR);
        fill(lv, x0 + 1, y0, x1 - 1, y0, TILE_WALL_H);
        fill(lv, x0 + 1, y1, x1 - 1, y1, TILE_WALL_H);
        fill(lv, x0, y0 + 1, x0, y1 - 1, TILE_WALL_V);
        fill(lv, x1, y0 + 1, x1, y1 - 1, TILE_WALL_V);
        level_set_tile(lv, x0, y0, TILE_CORNER_NW);
        level_set_tile(lv, x1, y0, TILE_CORNER_NE);
        level_set_tile(lv, x0, y1, TILE_CORNER_SW);
        level_set_tile(lv, x1, y1, TILE_CORNER_SE);
        if (argc > 5) {
            if (lv->section_count >= LEVEL_MAX_SECTIONS) return fail(c, "too many sections");
            section_t* s = &lv->sections[lv->section_count++];
            *s = (section_t){ x0, y0, x1 - x0 + 1, y1 - y0 + 1, "" };
            join_name(argv, 5, argc, s->name);
        }
    } else if (strcmp(cmd, "section") == 0) {
        if (argc < 6 || need_ints(c, argv, 1, 4, a)) return argc < 6 ? fail(c, "section X Y W H NAME") : -1;
        if (lv->section_count >= LEVEL_MAX_SECTIONS) return fail(c, "too many sections");
        section_t* s = &lv->sections[lv->section_count++];
        *s = (section_t){ a[0] + ox, a[1] + oy, a[2], a[3], "" };
        join_name(argv, 5, argc, s->name);
    } else if (strcmp(cmd, "cover") == 0) {
        // grow the reveal section next to cell X Y so it covers the cell
        // (walls moved onto their map line turn wall cells into floor)
        if (argc != 3 || need_ints(c, argv, 1, 2, a)) return argc != 3 ? fail(c, "cover X Y") : -1;
        int x = a[0] + ox, y = a[1] + oy;
        static const int nb[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        section_t* hit = NULL;
        for (int k = 0; k < 5 && !hit; k++) {
            for (int i = 0; i < lv->section_count && !hit; i++) {
                section_t* s = &lv->sections[i];
                int nx = x + nb[k][0], ny = y + nb[k][1];
                if (nx >= s->x && nx < s->x + s->w && ny >= s->y && ny < s->y + s->h) hit = s;
            }
        }
        if (!hit) return fail(c, "cover: no section next to %d %d", x, y);
        if (x < hit->x) { hit->w += hit->x - x; hit->x = x; }
        if (y < hit->y) { hit->h += hit->y - y; hit->y = y; }
        if (x >= hit->x + hit->w) hit->w = x - hit->x + 1;
        if (y >= hit->y + hit->h) hit->h = y - hit->y + 1;
    } else if (strcmp(cmd, "autowall") == 0) {
        autowall(lv);
    } else if (strcmp(cmd, "start") == 0) {
        if (argc != 3 || need_ints(c, argv, 1, 2, a)) return argc != 3 ? fail(c, "start X Y") : -1;
        lv->start_x = a[0] + ox;
        lv->start_y = a[1] + oy;
        obj_t* s = NULL;
        for (int i = 0; i < lv->obj_count; i++)
            if (lv->objs[i].kind == OBJ_START) s = &lv->objs[i];
        if (!s) {
            s = new_obj(c, OBJ_START, 0, 0);
            if (!s) return fail(c, "too many objects");
            snprintf(s->name, LEVEL_MAX_NAME, "Grace start");
        }
        s->x = lv->start_x;
        s->y = lv->start_y;
    } else if (strcmp(cmd, "door") == 0) {
        // door X Y h|v SPAN [props]: door tiles on a wall run; the door sits
        // on the edge at the top (h) or left (v) of its cells
        if (argc < 5 || need_ints(c, argv, 1, 2, a) || need_ints(c, argv, 4, 1, a + 2))
            return argc < 5 ? fail(c, "door X Y h|v SPAN [key=value...]") : -1;
        bool h = argv[3][0] == 'h';
        if (!h && argv[3][0] != 'v') return fail(c, "door orientation must be h or v");
        int x = a[0] + ox, y = a[1] + oy;
        obj_t* o = new_obj(c, OBJ_DOOR, x, y);
        if (!o) return fail(c, "too many objects");
        o->horizontal = h ? 1 : 0;
        o->span = a[2] < 1 ? 1 : a[2];
        o->state = DOOR_UNLOCKED;
        snprintf(o->name, LEVEL_MAX_NAME, "door");
        if (h) fill(lv, x, y, x + o->span - 1, y, TILE_FLOOR);
        else fill(lv, x, y, x, y + o->span - 1, TILE_FLOOR);
        if (apply_props(c, o, argv, 5, argc)) return -1;
    } else if (strcmp(cmd, "item") == 0) {
        if (argc < 4 || need_ints(c, argv, 1, 2, a)) return argc < 4 ? fail(c, "item X Y TYPE [key=value...]") : -1;
        obj_t* o = new_obj(c, OBJ_ITEM, a[0] + ox, a[1] + oy);
        if (!o) return fail(c, "too many objects");
        o->item_type = item_from_name(argv[3]);
        if (strcmp(item_name((item_t)o->item_type), argv[3]) != 0) return fail(c, "unknown item '%s'", argv[3]);
        snprintf(o->name, LEVEL_MAX_NAME, "item");
        if (apply_props(c, o, argv, 4, argc)) return -1;
    } else if (strcmp(cmd, "obj") == 0) {
        if (argc < 4 || need_ints(c, argv, 2, 2, a)) return argc < 4 ? fail(c, "obj KIND X Y [key=value...]") : -1;
        obj_kind_t k = obj_kind_from_name(argv[1]);
        if (strcmp(obj_kind_name(k), argv[1]) != 0 && !(k == OBJ_START && strcmp(argv[1], "start") == 0))
            return fail(c, "unknown object kind '%s'", argv[1]);
        obj_t* o = NULL;
        if (k == OBJ_START) {
            // one Player Spawn per level: a second one moves it
            for (int i = 0; i < lv->obj_count; i++)
                if (lv->objs[i].kind == OBJ_START) o = &lv->objs[i];
            if (o) { o->x = a[0] + ox; o->y = a[1] + oy; }
        }
        if (!o) {
            o = new_obj(c, k, a[0] + ox, a[1] + oy);
            if (!o) return fail(c, "too many objects");
            snprintf(o->name, LEVEL_MAX_NAME, "%s", k == OBJ_START ? "Grace start" : argv[1]);
        }
        if (apply_props(c, o, argv, 4, argc)) return -1;
        if (k == OBJ_START) { lv->start_x = o->x; lv->start_y = o->y; }
    } else if (strcmp(cmd, "link") == 0) {
        if (argc != 3) return fail(c, "link NAME_FROM NAME_TO (use _ for spaces)");
        obj_t* f = find_named(lv, argv[1]);
        obj_t* t = find_named(lv, argv[2]);
        if (!f || !t) return fail(c, "link: no object named '%s'", f ? argv[2] : argv[1]);
        f->link_id = t->id;
    } else if (strcmp(cmd, "set") == 0) {
        // set X Y key=value...: change the objects anchored on that cell
        if (argc < 4 || need_ints(c, argv, 1, 2, a)) return argc < 4 ? fail(c, "set X Y key=value...") : -1;
        int hit = 0;
        for (int i = 0; i < lv->obj_count; i++) {
            obj_t* o = &lv->objs[i];
            if (o->x != a[0] + ox || o->y != a[1] + oy) continue;
            char* copy[MAX_ARGS];
            char store[MAX_ARGS][128];
            for (int k = 3; k < argc; k++) {
                snprintf(store[k], sizeof(store[k]), "%s", argv[k]);
                copy[k] = store[k];
            }
            if (apply_props(c, o, copy, 3, argc)) return -1;
            hit++;
        }
        if (!hit) return fail(c, "set: no object at %d %d", a[0], a[1]);
    } else if (strcmp(cmd, "setnear") == 0) {
        // setnear KIND X Y R key=value...: change the nearest KIND object
        // within R cells, so a script survives a re-trace that moves it
        if (argc < 6 || need_ints(c, argv, 2, 3, a)) return argc < 6 ? fail(c, "setnear KIND X Y R key=value...") : -1;
        obj_kind_t k = obj_kind_from_name(argv[1]);
        if ((int)k < 0) return fail(c, "unknown object kind '%s'", argv[1]);
        obj_t* best = NULL;
        int bd = 0;
        for (int i = 0; i < lv->obj_count; i++) {
            obj_t* o = &lv->objs[i];
            int dx = o->x - (a[0] + ox), dy = o->y - (a[1] + oy);
            int d = dx * dx + dy * dy;
            if (o->kind != k || d > a[2] * a[2] || (best && d >= bd)) continue;
            best = o;
            bd = d;
        }
        if (!best) return fail(c, "setnear: no %s within %d of %d %d", argv[1], a[2], a[0], a[1]);
        char* copy[MAX_ARGS];
        char store[MAX_ARGS][128];
        for (int j = 5; j < argc; j++) {
            snprintf(store[j], sizeof(store[j]), "%s", argv[j]);
            copy[j] = store[j];
        }
        if (apply_props(c, best, copy, 5, argc)) return -1;
    } else if (strcmp(cmd, "cut") == 0) {
        // cut X0 Y0 X1 Y1: remove a part of the map. Its cells become void,
        // every object anchored in it goes, every section inside it goes,
        // and links to the removed objects are cleared (nothing dangles).
        if (argc != 5 || need_ints(c, argv, 1, 4, a)) return argc != 5 ? fail(c, "cut X0 Y0 X1 Y1") : -1;
        int x0 = a[0] + ox, y0 = a[1] + oy, x1 = a[2] + ox, y1 = a[3] + oy;
        order(&x0, &x1);
        order(&y0, &y1);
        fill(lv, x0, y0, x1, y1, TILE_VOID);
        int gone[LEVEL_MAX_OBJECTS], ngone = 0, w = 0;
        for (int i = 0; i < lv->obj_count; i++) {
            const obj_t* o = &lv->objs[i];
            if (o->x >= x0 && o->x <= x1 && o->y >= y0 && o->y <= y1) gone[ngone++] = o->id;
            else lv->objs[w++] = lv->objs[i];
        }
        lv->obj_count = w;
        for (int i = 0; i < lv->obj_count; i++)
            for (int k = 0; k < ngone; k++)
                if (lv->objs[i].link_id == gone[k]) lv->objs[i].link_id = 0;
        w = 0;
        for (int i = 0; i < lv->section_count; i++) {
            const section_t* s = &lv->sections[i];
            bool inside = s->x >= x0 && s->y >= y0 && s->x + s->w - 1 <= x1 && s->y + s->h - 1 <= y1;
            if (!inside) lv->sections[w++] = lv->sections[i];
        }
        lv->section_count = w;
    } else if (strcmp(cmd, "erase") == 0) {
        // erase X Y: remove every object anchored on that cell
        if (argc != 3 || need_ints(c, argv, 1, 2, a)) return argc != 3 ? fail(c, "erase X Y") : -1;
        int w = 0;
        for (int i = 0; i < lv->obj_count; i++)
            if (!(lv->objs[i].x == a[0] + ox && lv->objs[i].y == a[1] + oy)) lv->objs[w++] = lv->objs[i];
        lv->obj_count = w;
    } else {
        return fail(c, "unknown command '%s'", cmd);
    }
    return 0;
}

int levelscript_run(level_t* lv, const char* text, char* err, int errcap) {
    ctx_t c = { lv, 0, 0, err, errcap, 0 };
    if (err && errcap > 0) err[0] = 0;
    int ran = 0;
    const char* p = text;
    while (*p) {
        c.line++;
        const char* end = strchr(p, '\n');
        int len = end ? (int)(end - p) : (int)strlen(p);
        char buf[MAX_LINE];
        if (len >= MAX_LINE) return fail(&c, "line too long");
        memcpy(buf, p, (size_t)len);
        buf[len] = 0;
        p += len + (end ? 1 : 0);
        char* hash = strchr(buf, '#');
        if (hash) *hash = 0;
        char* argv[MAX_ARGS];
        int argc = 0;
        for (char* t = strtok(buf, " \t\r"); t && argc < MAX_ARGS; t = strtok(NULL, " \t\r")) argv[argc++] = t;
        if (argc == 0) continue;
        if (run_line(&c, argc, argv) < 0) return -1;
        ran++;
    }
    if (lv->has_tiles) level_compile_tiles(lv);
    return ran;
}
