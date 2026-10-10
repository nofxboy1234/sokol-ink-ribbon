#include "items.h"
#include "grid.h"
#include "inventory.h"
#include "level.h"
#include "player.h"

#include <string.h>

static struct {
    unsigned char taken[LEVEL_MAX_OBJECTS];
    int taken_count;
} it;

void items_init(void) {
    memset(&it, 0, sizeof(it));
}

bool items_take(int object_index) {
    const level_t* lv = grid_level();
    if (object_index < 0 || object_index >= lv->obj_count || it.taken[object_index]) {
        return false;
    }
    const obj_t* o = &lv->objs[object_index];
    if (o->kind != OBJ_ITEM) {
        return false;
    }
    if (!inventory_add(o->item_type)) {
        return false;
    }
    it.taken[object_index] = 1;
    it.taken_count++;
    return true;
}

bool items_taken(int object_index) {
    if (object_index < 0 || object_index >= LEVEL_MAX_OBJECTS) {
        return false;
    }
    return it.taken[object_index] != 0;
}

// True if the item object with this id has been collected (used by links).
bool items_taken_by_id(int id) {
    const level_t* lv = grid_level();
    for (int i = 0; i < lv->obj_count; i++) {
        if (lv->objs[i].id == id && lv->objs[i].kind == OBJ_ITEM && it.taken[i]) {
            return true;
        }
    }
    return false;
}

int items_taken_count(void) {
    return it.taken_count;
}
