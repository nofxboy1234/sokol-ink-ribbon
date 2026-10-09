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

bool items_update(void) {
    const level_t* lv = grid_level();
    int px, py;
    player_cell(&px, &py);
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_ITEM || it.taken[i]) {
            continue;
        }
        if (o->x == px && o->y == py) {
            if (inventory_add(o->item_type)) {
                it.taken[i] = 1;
                it.taken_count++;
                return true;
            }
        }
    }
    return false;
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
