#include "doors.h"
#include "grid.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "player.h"

#include <string.h>

#define DOOR_DISCOVER_DIST 3.0f

static struct {
    level_t* level;
    unsigned char discovered[LEVEL_MAX_OBJECTS];
    int discovered_count;
    float close_timer[LEVEL_MAX_OBJECTS];
} dr;

void doors_init(void) {
    memset(&dr, 0, sizeof(dr));
    dr.level = (level_t*)grid_level();
}

static bool is_door(const obj_t* o) {
    return o->kind == OBJ_DOOR;
}

static bool blocks(const obj_t* o) {
    // unlocked doors never block movement; Grace opens them by walking through
    return o->state != DOOR_UNLOCKED && o->state != DOOR_BROKEN;
}

// Only a shut door can be smashed: an open one is walked through and a
// broken one is already gone.
static bool intact_breakable(const obj_t* o) {
    return is_door(o) && o->breakable && o->state != DOOR_BROKEN && !o->open;
}

static bool on_edge(const obj_t* o, int ax, int ay, int bx, int by) {
    int span = o->span > 0 ? o->span : 1;
    for (int k = 0; k < span; k++) {
        if (o->horizontal) {
            // on the horizontal wall line at row y, between (x, y-1) and (x, y)
            int x = o->x + k;
            if (ax == x && bx == x && ((ay == o->y - 1 && by == o->y) || (ay == o->y && by == o->y - 1))) {
                return true;
            }
        } else {
            // on the vertical wall line at column x, between (x-1, y) and (x, y)
            int y = o->y + k;
            if (ay == y && by == y && ((ax == o->x - 1 && bx == o->x) || (ax == o->x && bx == o->x - 1))) {
                return true;
            }
        }
    }
    return false;
}

bool doors_on_edge(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        if (is_door(&lv->objs[i]) && on_edge(&lv->objs[i], ax, ay, bx, by)) {
            return true;
        }
    }
    return false;
}

// Closed doors block sight; open and broken ones leave a gap to look through.
bool doors_block_sight(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (is_door(o) && o->state != DOOR_BROKEN && !o->open && on_edge(o, ax, ay, bx, by)) {
            return true;
        }
    }
    return false;
}

int doors_closed_on_edge(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (is_door(o) && o->state != DOOR_BROKEN && !o->open && on_edge(o, ax, ay, bx, by)) {
            return i;
        }
    }
    return -1;
}

bool doors_breakable_edge(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (intact_breakable(o) && on_edge(o, ax, ay, bx, by)) {
            return true;
        }
    }
    return false;
}

void doors_open(int index) {
    level_t* lv = dr.level;
    if (index >= 0 && index < lv->obj_count && lv->objs[index].state == DOOR_UNLOCKED) {
        lv->objs[index].open = 1;
    }
}

void doors_break(int index) {
    level_t* lv = dr.level;
    if (index < 0 || index >= lv->obj_count || !intact_breakable(&lv->objs[index])) {
        return;
    }
    lv->objs[index].state = DOOR_BROKEN;
    lv->objs[index].open = 1;
    dr.discovered[index] = 1;
}

bool doors_block_edge(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (is_door(o) && blocks(o) && on_edge(o, ax, ay, bx, by)) {
            return true;
        }
    }
    return false;
}

// Chebyshev distance to the cells on either side of the door. The slab sits on
// the wall line, so both neighbouring rows (or columns) are one cell away.
static int cell_door_distance(const obj_t* o, int x, int y) {
    int span = o->span > 0 ? o->span : 1;
    int x0, x1, y0, y1;
    if (o->horizontal) {
        x0 = o->x;
        x1 = o->x + span - 1;
        y0 = o->y - 1;
        y1 = o->y;
    } else {
        x0 = o->x - 1;
        x1 = o->x;
        y0 = o->y;
        y1 = o->y + span - 1;
    }
    int nx = x < x0 ? x0 : (x > x1 ? x1 : x);
    int ny = y < y0 ? y0 : (y > y1 ? y1 : y);
    int adx = nx > x ? nx - x : x - nx;
    int ady = ny > y ? ny - y : y - ny;
    return adx > ady ? adx : ady;
}

// Locked and unknown doors keep Grace two cells away so her dot does not
// overlap the slab. Unlocked doors have no such limit: she walks through an
// open one, and pushes a closed one open by stepping up to it.
bool doors_approach_blocked(int from_x, int from_y, int to_x, int to_y, bool through_breakable) {
    (void)from_x;
    (void)from_y;
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (!is_door(o) || !blocks(o) || (through_breakable && intact_breakable(o))) {
            continue;
        }
        if (cell_door_distance(o, to_x, to_y) == 0) {
            return true;
        }
    }
    return false;
}

static int linked_item_type(const obj_t* o) {
    if (o->link_id <= 0) {
        return -1;
    }
    const level_t* lv = dr.level;
    for (int k = 0; k < lv->obj_count; k++) {
        const obj_t* s = &lv->objs[k];
        if (s->id == o->link_id && s->kind == OBJ_ITEM) {
            return s->item_type;
        }
    }
    return -1;
}

// The item type that unlocks this door or openable, or -1 if none does.
int doors_key_type(const obj_t* o) {
    if (o->kind == OBJ_DOOR && o->key_id > 0) {
        return o->key_id;
    }
    return linked_item_type(o);
}

static bool will_be_locked(const obj_t* d) {
    if (d->state == DOOR_LOCKED) {
        return true;
    }
    if (d->state != DOOR_UNKNOWN) {
        return false;
    }
    if (d->discover_state == DOOR_LOCKED) {
        return true;
    }
    return d->discover_state != DOOR_UNLOCKED && d->key_id > 0;
}

void doors_drop_key_if_done(int item_type) {
    if (item_type < 0) {
        return;
    }
    const level_t* lv = dr.level;
    for (int k = 0; k < lv->obj_count; k++) {
        const obj_t* d = &lv->objs[k];
        bool waiting = (is_door(d) && will_be_locked(d)) ||
                       (d->kind == OBJ_OPENABLE && !d->open);
        if (waiting && doors_key_type(d) == item_type) {
            return;
        }
    }
    inventory_remove(item_type);
}

static int pending_code_door = -1;
int doors_pending_code(void) { return pending_code_door; }
bool doors_try_code(int code) {
    level_t* lv = dr.level;
    if (pending_code_door < 0 || pending_code_door >= lv->obj_count) return false;
    obj_t* o = &lv->objs[pending_code_door];
    if (o->kind != OBJ_DOOR || o->code != code) return false;
    o->state = DOOR_UNLOCKED; o->open = 0; dr.discovered[pending_code_door] = 1; pending_code_door = -1;
    return true;
}
bool doors_interact(int x, int y) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (!is_door(o) || o->state == DOOR_BROKEN) {
            continue;
        }
        int span = o->span > 0 ? o->span : 1;
        bool covered = o->horizontal
            ? ((y == o->y || y == o->y - 1) && x >= o->x && x < o->x + span)
            : ((x == o->x || x == o->x - 1) && y >= o->y && y < o->y + span);
        if (!covered || interact_distance(i) > 2) {
            continue;
        }
        if (o->state == DOOR_LOCKED && o->code > 0) { pending_code_door = i; return true; }
        int key = doors_key_type(o);
        if (o->state == DOOR_LOCKED && key >= 0 && inventory_has(key)) {
            o->state = DOOR_UNLOCKED;
            o->open = 0;
            dr.discovered[i] = 1;
            doors_drop_key_if_done(key);
            return true;
        }
        if (o->state == DOOR_UNLOCKED) {
            o->open = !o->open;
            if (o->open && o->auto_close > 0.0f) {
                dr.close_timer[i] = o->auto_close;
            }
            return true;
        }
        return false;
    }
    return false;
}

bool doors_update(float dt) {
    level_t* lv = dr.level;
    bool changed = false;
    int px, py;
    player_cell(&px, &py);
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (!is_door(o)) {
            continue;
        }
        if (!dr.discovered[i]) {
            float dx = (float)(o->x - px);
            float dy = (float)(o->y - py);
            if (dx * dx + dy * dy <= DOOR_DISCOVER_DIST * DOOR_DISCOVER_DIST) {
                dr.discovered[i] = 1;
                dr.discovered_count++;
                if (o->state == DOOR_UNKNOWN) {
                    if (o->discover_state == DOOR_LOCKED || o->discover_state == DOOR_UNLOCKED) {
                        o->state = (door_state_t)o->discover_state;
                    } else {
                        o->state = o->key_id > 0 ? DOOR_LOCKED : DOOR_UNLOCKED;
                    }
                }
                changed = true;
            }
        }
        if (o->state == DOOR_UNLOCKED && !o->open) {
            // Grace opens an unlocked door by walking up to or through it
            bool on = false;
            int span = o->span > 0 ? o->span : 1;
            for (int k = 0; k < span && !on; k++) {
                if (o->horizontal) {
                    int x = o->x + k;
                    on = px == x && (py == o->y || py == o->y - 1);
                } else {
                    int y = o->y + k;
                    on = py == y && (px == o->x || px == o->x - 1);
                }
            }
            if (on) {
                o->open = 1;
                changed = true;
            }
        }
        if (o->state == DOOR_UNLOCKED && o->open && o->auto_close > 0.0f) {
            dr.close_timer[i] -= dt;
            if (dr.close_timer[i] <= 0.0f) {
                o->open = 0;
                changed = true;
            }
        }
    }
    return changed;
}

int doors_discovered_count(void) {
    return dr.discovered_count;
}

// After loading a save, mark doors whose state is already known as discovered.
void doors_restore_from_level(void) {
    level_t* lv = dr.level;
    dr.discovered_count = 0;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        dr.discovered[i] = 0;
        if (o->kind == OBJ_DOOR && o->state != DOOR_UNKNOWN) {
            dr.discovered[i] = 1;
            dr.discovered_count++;
        }
    }
}
