#include "interact.h"
#include "doors.h"
#include "grid.h"
#include "inventory.h"
#include "items.h"
#include "light.h"
#include "level.h"
#include "player.h"

#include <string.h>

static struct {
    level_t* level;
    bool lighter;
    int file_obj[LEVEL_MAX_OBJECTS];
    int file_count;
    int saves;
    bool goal;
    bool save_menu;
} in;

static bool rect_covers(const obj_t* o, int x, int y);
int interact_distance(int object_index);

void interact_init(void) {
    memset(&in, 0, sizeof(in));
    in.level = (level_t*)grid_level();
}

// A switch toggles the lights linked to it (a light's "link" names its
// switch). A switch with no linked lights toggles the unlinked lights in its
// group, as before.
static void toggle_switch(const obj_t* sw) {
    bool linked = false;
    for (int i = 0; i < in.level->obj_count; i++) {
        obj_t* o = &in.level->objs[i];
        if (o->kind == OBJ_LIGHT && o->link_id == sw->id) {
            o->state = !o->state;
            linked = true;
        }
    }
    for (int i = 0; i < in.level->obj_count; i++) {
        obj_t* o = &in.level->objs[i];
        if (!linked && o->kind == OBJ_LIGHT && o->link_id <= 0 && o->group_id == sw->group_id) {
            o->state = !o->state;
        }
        if (o->kind == OBJ_SWITCH && (linked ? o == sw : o->group_id == sw->group_id)) {
            o->state = !o->state;
        }
    }
    light_update();
}

static bool file_found(int obj_index) {
    for (int i = 0; i < in.file_count; i++) {
        if (in.file_obj[i] == obj_index) {
            return true;
        }
    }
    return false;
}

bool interact_at(int x, int y) {
    for (int i = 0; i < in.level->obj_count; i++) {
        obj_t* o = &in.level->objs[i];
        bool covered = (o->kind == OBJ_OPENABLE || o->kind == OBJ_OBSTACLE)
            ? rect_covers(o, x, y)
            : (o->x == x && o->y == y);
        if (!covered) {
            continue;
        }
        if (o->kind == OBJ_ITEM && openable_covers_closed(o->x, o->y)) {
            continue;
        }
        if (o->kind == OBJ_ITEM && !light_cell_lit(o->x, o->y)) {
            continue;  // unlit items can't be seen, so they can't be picked up
        }
        if (interact_distance(i) > 1) {
            continue;
        }
        if (o->kind == OBJ_ITEM) {
            // picking an item up only happens on an explicit click
            items_take(i);
            return true;
        }
        if (o->kind == OBJ_OPENABLE && !o->open) {
            int keytype = doors_key_type(o);
            if (keytype >= 0 && !inventory_has(keytype)) {
                return true; // needs the linked key
            }
            o->open = 1;
            doors_drop_key_if_done(keytype);
            return true;
        }
        if (o->kind == OBJ_SWITCH) {
            toggle_switch(o);
            return true;
        }
        if (o->kind == OBJ_TYPEWRITER) {
            // saving spends an ink ribbon, so the menu only opens with one
            if (inventory_has(ITEM_INK_RIBBON)) {
                in.save_menu = true;
            }
            return true;
        }
        if (o->kind == OBJ_FILE && !file_found(i)) {
            if (in.file_count < LEVEL_MAX_OBJECTS) {
                in.file_obj[in.file_count++] = i;
            }
            return true;
        }
        if (o->kind == OBJ_SAFE && !file_found(i)) {
            if (in.file_count < LEVEL_MAX_OBJECTS) {
                in.file_obj[in.file_count++] = i;
            }
            return true;
        }
        if (o->kind == OBJ_FUSEBOX && !in.goal) {
            if (o->link_id > 0 && !items_taken_by_id(o->link_id)) {
                return true;
            }
            if (inventory_has(ITEM_FUSE)) {
                inventory_remove(ITEM_FUSE);
            }
            in.goal = true;
            return true;
        }
    }
    return false;
}

static bool rect_covers(const obj_t* o, int x, int y) {
    int w = o->w > 0 ? o->w : 1;
    int h = o->h > 0 ? o->h : 1;
    return x >= o->x && x < o->x + w && y >= o->y && y < o->y + h;
}

// Chebyshev distance from Grace to an object; for an item sitting on an
// obstacle/openable, the support's edge counts too (so she can reach items
// on top of a table she is standing against).
int interact_distance(int object_index) {
    const level_t* lv = in.level;
    if (object_index < 0 || object_index >= lv->obj_count) {
        return 99;
    }
    const obj_t* o = &lv->objs[object_index];
    int px, py;
    player_cell(&px, &py);
    int ex = o->x;
    int ey = o->y;
    if (o->kind == OBJ_OPENABLE || o->kind == OBJ_OBSTACLE) {
        ex = o->x + (o->w > 0 ? o->w : 1) - 1;
        ey = o->y + (o->h > 0 ? o->h : 1) - 1;
    } else if (o->kind == OBJ_DOOR) {
        int span = o->span > 0 ? o->span : 1;
        if (o->horizontal) {
            ex = o->x + span - 1;
            ey = o->y;
        } else {
            ex = o->x;
            ey = o->y + span - 1;
        }
    }
    int nx = px < o->x ? o->x : (px > ex ? ex : px);
    int ny = py < o->y ? o->y : (py > ey ? ey : py);
    int dx = nx - px;
    int dy = ny - py;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int d = adx > ady ? adx : ady;
    if (o->kind == OBJ_ITEM) {
        for (int k = 0; k < lv->obj_count; k++) {
            const obj_t* s = &lv->objs[k];
            if (s->kind != OBJ_OPENABLE && s->kind != OBJ_OBSTACLE) {
                continue;
            }
            if (!rect_covers(s, o->x, o->y)) {
                continue;
            }
            int sex = s->x + (s->w > 0 ? s->w : 1) - 1;
            int sey = s->y + (s->h > 0 ? s->h : 1) - 1;
            int snx = px < s->x ? s->x : (px > sex ? sex : px);
            int sny = py < s->y ? s->y : (py > sey ? sey : py);
            int sdx = snx - px;
            int sdy = sny - py;
            int sadx = sdx < 0 ? -sdx : sdx;
            int sady = sdy < 0 ? -sdy : sdy;
            int sd = sadx > sady ? sadx : sady;
            if (sd < d) {
                d = sd;
            }
        }
    }
    return d;
}

bool obstacles_block_cell(int x, int y) {
    for (int i = 0; i < in.level->obj_count; i++) {
        const obj_t* o = &in.level->objs[i];
        if (o->kind == OBJ_OBSTACLE && !o->climbable && rect_covers(o, x, y)) {
            return true;
        }
        // open or closed, the openable's cells stay solid so Grace walks around it
        if (o->kind == OBJ_OPENABLE && rect_covers(o, x, y)) {
            return true;
        }
    }
    return false;
}

// True if a closed openable covers this cell (its contents are hidden).
bool openable_covers_closed(int x, int y) {
    for (int i = 0; i < in.level->obj_count; i++) {
        const obj_t* o = &in.level->objs[i];
        if (o->kind == OBJ_OPENABLE && !o->open && rect_covers(o, x, y)) {
            return true;
        }
    }
    return false;
}

bool lighter_on(void) {
    return in.lighter;
}

void lighter_toggle(void) {
    in.lighter = !in.lighter;
    light_update();
}

int files_found(void) {
    return in.file_count;
}

const char* file_name_at(int index) {
    if (index < 0 || index >= in.file_count) {
        return "";
    }
    return in.level->objs[in.file_obj[index]].name;
}

int file_code_at(int index) {
    if (index < 0 || index >= in.file_count) {
        return 0;
    }
    return in.level->objs[in.file_obj[index]].code;
}

int saves_made(void) {
    return in.saves;
}

bool goal_met(void) {
    return in.goal;
}

bool interact_take_save_menu(void) {
    bool open = in.save_menu;
    in.save_menu = false;
    return open;
}

void interact_export(bool* lighter, int* saves, bool* goal, int* file_count, int* file_obj) {
    *lighter = in.lighter;
    *saves = in.saves;
    *goal = in.goal;
    *file_count = in.file_count;
    for (int i = 0; i < in.file_count; i++) {
        file_obj[i] = in.file_obj[i];
    }
}

void interact_restore(bool lighter, int saves, bool goal, int file_count, const int* file_obj) {
    in.lighter = lighter;
    in.saves = saves;
    in.goal = goal;
    in.file_count = file_count < 0 ? 0 : file_count;
    for (int i = 0; i < in.file_count; i++) {
        in.file_obj[i] = file_obj[i];
    }
}
