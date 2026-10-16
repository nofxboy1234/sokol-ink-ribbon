#include "light.h"
#include "grid.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "player.h"
#include "sight.h"

#include <math.h>
#include <string.h>

static unsigned char lit[GRID_MAX_ROWS][GRID_MAX_COLS];

float light_radius_of(float radius) {
    return radius > 0.0f ? radius : LIGHT_DEFAULT_RADIUS;
}

static void shine(int sx, int sy, float radius) {
    int r = (int)ceilf(radius);
    for (int y = sy - r; y <= sy + r; y++) {
        for (int x = sx - r; x <= sx + r; x++) {
            if (!grid_in_bounds(x, y) || lit[y][x] || !grid_is_floor(x, y)) {
                continue;
            }
            float dx = (float)(x - sx), dy = (float)(y - sy);
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            if ((x == sx && y == sy) || sight_clear(sx, sy, x, y)) {
                lit[y][x] = 1;
            }
        }
    }
}

void light_update(void) {
    memset(lit, 0, sizeof(lit));
    if (lighter_on() && inventory_has(ITEM_LIGHTER)) {
        int px, py;
        player_cell(&px, &py);
        shine(px, py, LIGHTER_RADIUS_CELLS);
    }
    const level_t* lv = grid_level();
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind == OBJ_LIGHT && o->state) {
            shine(o->x, o->y, light_radius_of(o->radius));
        }
    }
    // whatever any light (the Lighter or a switched-on lamp) shows stays on the map
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind == OBJ_ITEM && light_cell_lit(o->x, o->y)) {
            items_mark_seen(i);
        }
    }
}

bool light_cell_lit(int x, int y) {
    return grid_in_bounds(x, y) && lit[y][x] != 0;
}

bool light_item_visible(int object_index) {
    const level_t* lv = grid_level();
    if (object_index < 0 || object_index >= lv->obj_count) {
        return false;
    }
    const obj_t* o = &lv->objs[object_index];
    return light_cell_lit(o->x, o->y) || items_seen(object_index);
}
