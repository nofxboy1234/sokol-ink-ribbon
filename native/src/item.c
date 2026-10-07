#include "item.h"
#include "grid.h"
#include "inventory.h"
#include "player.h"
#include <stdlib.h>

typedef struct {
    int cell_x;
    int cell_y;
    bool taken;
} item_t;

// Green Herb sits in the left room so Grace has to walk over to reach it.
static item_t items[ITEM_COUNT] = {
    { 2, 6, false },
};

static bool valid(int item) {
    return (item >= 0) && (item < ITEM_COUNT);
}

void items_init(void) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        items[i].taken = false;
    }
}

int item_count(void) {
    return ITEM_COUNT;
}

bool item_present(int item) {
    return valid(item) && !items[item].taken;
}

int item_cell_x(int item) {
    return items[item].cell_x;
}

int item_cell_y(int item) {
    return items[item].cell_y;
}

vec2_t item_position(int item) {
    return grid_cell_center(items[item].cell_x, items[item].cell_y);
}

bool item_in_reach(int item) {
    if (!item_present(item)) {
        return false;
    }
    int px, py;
    player_cell(&px, &py);
    const int dx = px - items[item].cell_x;
    const int dy = py - items[item].cell_y;
    // Grace can pick it up when she stands on it or in a neighbouring cell
    return (abs(dx) + abs(dy)) <= 1;
}

bool item_can_take(int item) {
    return item_in_reach(item) && !inventory_full();
}

int item_any_in_reach(void) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (item_in_reach(i)) {
            return i;
        }
    }
    return -1;
}

bool item_take(int item) {
    if (!valid(item) || items[item].taken) {
        return false;
    }
    // a full inventory leaves the item lying on the ground
    if (!inventory_add(item)) {
        return false;
    }
    items[item].taken = true;
    return true;
}