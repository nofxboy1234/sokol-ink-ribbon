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

int items_taken_count(void) {
    return it.taken_count;
}
