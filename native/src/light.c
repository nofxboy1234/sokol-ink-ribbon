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

bool light_is_on(const obj_t* o) {
    return o->kind == OBJ_LIGHT && o->state == LIGHT_ON;
}

bool light_is_broken(const obj_t* o) {
    return o->kind == OBJ_LIGHT && o->state == LIGHT_BROKEN;
}

bool light_is_breakable_now(const obj_t* o) {
    return o->kind == OBJ_LIGHT && o->breakable && o->state == LIGHT_ON;
}

int light_breakable_near(float x, float y, float r) {
    const level_t* lv = grid_level();
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (!light_is_breakable_now(o)) {
            continue;
        }
        float dx = (float)o->x + 0.5f - x;
        float dy = (float)o->y + 0.5f - y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= r * r && sight_clear(cx, cy, o->x, o->y) && (best < 0 || d2 < best_d2)) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

void light_toggle(int index) {
    level_t* lv = (level_t*)grid_level();
    if (index < 0 || index >= lv->obj_count) {
        return;
    }
    obj_t* o = &lv->objs[index];
    if (o->kind != OBJ_LIGHT || o->state == LIGHT_BROKEN) {
        return;
    }
    o->state = o->state == LIGHT_ON ? LIGHT_OFF : LIGHT_ON;
}

void light_break(int index) {
    level_t* lv = (level_t*)grid_level();
    if (index < 0 || index >= lv->obj_count) {
        return;
    }
    obj_t* o = &lv->objs[index];
    if (!light_is_breakable_now(o)) {
        return;
    }
    o->state = LIGHT_BROKEN;
}

int light_broken_count(void) {
    const level_t* lv = grid_level();
    int count = 0;
    for (int i = 0; i < lv->obj_count; i++) {
        count += light_is_broken(&lv->objs[i]);
    }
    return count;
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
        if (light_is_on(o)) {
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
