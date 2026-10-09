#include "interact.h"
#include "grid.h"
#include "inventory.h"
#include "level.h"
#include "player.h"

#include <string.h>

static struct {
    level_t* level;
    bool lighter;
    int file_obj[LEVEL_MAX_OBJECTS];
    int file_count;
    int saves;
} in;

void interact_init(void) {
    memset(&in, 0, sizeof(in));
    in.level = (level_t*)grid_level();
}

static void toggle_group(int group) {
    for (int i = 0; i < in.level->obj_count; i++) {
        obj_t* o = &in.level->objs[i];
        if (o->kind == OBJ_LIGHT && o->group_id == group) {
            o->state = !o->state;
        }
        if (o->kind == OBJ_SWITCH && o->group_id == group) {
            o->state = !o->state;
        }
    }
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
    int px, py;
    player_cell(&px, &py);
    int adx = x > px ? x - px : px - x;
    int ady = y > py ? y - py : py - y;
    if (adx > 1 || ady > 1) {
        return false;
    }
    for (int i = 0; i < in.level->obj_count; i++) {
        obj_t* o = &in.level->objs[i];
        if (o->x != x || o->y != y) {
            continue;
        }
        if (o->kind == OBJ_SWITCH) {
            toggle_group(o->group_id);
            return true;
        }
        if (o->kind == OBJ_TYPEWRITER) {
            if (inventory_has(ITEM_INK_RIBBON)) {
                in.saves++;
                return true;
            }
            return false;
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
    }
    return false;
}

bool obstacles_block_cell(int x, int y) {
    for (int i = 0; i < in.level->obj_count; i++) {
        const obj_t* o = &in.level->objs[i];
        if (o->kind == OBJ_OBSTACLE && o->x == x && o->y == y && !o->climbable) {
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
