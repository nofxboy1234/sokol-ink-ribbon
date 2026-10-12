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
    return o->state != DOOR_UNLOCKED;
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

// Chebyshev distance from a cell to the door span, matching interact_distance.
static int cell_door_distance(const obj_t* o, int x, int y) {
    int span = o->span > 0 ? o->span : 1;
    int ex = o->horizontal ? o->x + span - 1 : o->x;
    int ey = o->horizontal ? o->y : o->y + span - 1;
    int nx = x < o->x ? o->x : (x > ex ? ex : x);
    int ny = y < o->y ? o->y : (y > ey ? ey : y);
    int adx = nx > x ? nx - x : x - nx;
    int ady = ny > y ? ny - y : y - ny;
    return adx > ady ? adx : ady;
}

// Locked and unknown doors keep Grace two cells away so her dot does not
// overlap the slab. Unlocked doors have no such limit: she walks through an
// open one, and pushes a closed one open by stepping up to it.
bool doors_approach_blocked(int from_x, int from_y, int to_x, int to_y) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (!is_door(o) || o->state == DOOR_UNLOCKED) {
            continue;
        }
        int to_d = cell_door_distance(o, to_x, to_y);
        if (to_d >= 2) {
            continue;
        }
        int from_d = cell_door_distance(o, from_x, from_y);
        if (from_d < 2 && to_d > from_d) {
            continue;
        }
        return true;
    }
    return false;
}

static bool key_available(const obj_t* o) {
    if (o->key_id <= 0) {
        return false;
    }
    return inventory_has(o->key_id);
}

bool doors_interact(int x, int y) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (!is_door(o)) {
            continue;
        }
        int span = o->span > 0 ? o->span : 1;
        bool covered = o->horizontal
            ? ((y == o->y || y == o->y - 1) && x >= o->x && x < o->x + span)
            : ((x == o->x || x == o->x - 1) && y >= o->y && y < o->y + span);
        if (!covered || interact_distance(i) > 2) {
            continue;
        }
        if (o->state == DOOR_LOCKED && (key_available(o) || items_taken_by_id(o->link_id))) {
            int key = o->key_id;
            o->state = DOOR_UNLOCKED;
            o->open = 0;
            dr.discovered[i] = 1;
            if (key > 0 && key_available(o)) {
                // drop the key once no locked door needs it any more
                bool needed = false;
                for (int k = 0; k < lv->obj_count; k++) {
                    const obj_t* d = &lv->objs[k];
                    if (is_door(d) && d->state == DOOR_LOCKED && d->key_id == key) {
                        needed = true;
                        break;
                    }
                }
                if (!needed) {
                    inventory_remove(key);
                }
            }
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
