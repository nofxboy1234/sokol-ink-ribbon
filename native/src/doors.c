#include "doors.h"
#include "grid.h"
#include "inventory.h"
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
    if (o->state == DOOR_UNLOCKED) {
        return !o->open;
    }
    return true;
}

bool doors_block_edge(int ax, int ay, int bx, int by) {
    level_t* lv = dr.level;
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (!is_door(o) || !blocks(o)) {
            continue;
        }
        if (o->horizontal) {
            // sits on the horizontal wall line at row y, between (x, y-1) and (x, y)
            if (ax == o->x && bx == o->x &&
                ((ay == o->y - 1 && by == o->y) || (ay == o->y && by == o->y - 1))) {
                return true;
            }
        } else {
            // sits on the vertical wall line at column x, between (x-1, y) and (x, y)
            if (ay == o->y && by == o->y &&
                ((ax == o->x - 1 && bx == o->x) || (ax == o->x && bx == o->x - 1))) {
                return true;
            }
        }
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
    int px, py;
    player_cell(&px, &py);
    int adx = x > px ? x - px : px - x;
    int ady = y > py ? y - py : py - y;
    if (adx > 1 || ady > 1) {
        return false;
    }
    for (int i = 0; i < lv->obj_count; i++) {
        obj_t* o = &lv->objs[i];
        if (!is_door(o) || o->x != x || o->y != y) {
            continue;
        }
        if (o->state == DOOR_LOCKED && key_available(o)) {
            o->state = DOOR_UNLOCKED;
            o->open = 1;
            dr.discovered[i] = 1;
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
                    o->state = o->key_id > 0 ? DOOR_LOCKED : DOOR_UNLOCKED;
                }
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
