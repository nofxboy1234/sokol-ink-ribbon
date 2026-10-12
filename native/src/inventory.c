#include "inventory.h"

// Fixed cells, so a move can leave an empty slot in the middle.
static int inv_slots[INV_SLOTS];

void inventory_init(void) {
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slots[i] = -1;
    }
}

bool inventory_add(int item_type) {
    for (int i = 0; i < INV_SLOTS; i++) {
        if (inv_slots[i] < 0) {
            inv_slots[i] = item_type;
            return true;
        }
    }
    return false;
}

bool inventory_remove(int item_type) {
    for (int i = 0; i < INV_SLOTS; i++) {
        if (inv_slots[i] == item_type) {
            inv_slots[i] = -1;
            return true;
        }
    }
    return false;
}

bool inventory_move(int from, int to) {
    if (from < 0 || to < 0 || from >= INV_SLOTS || to >= INV_SLOTS || from == to) {
        return false;
    }
    if (inv_slots[from] < 0) {
        return false;
    }
    int item = inv_slots[from];
    inv_slots[from] = inv_slots[to];
    inv_slots[to] = item;
    return true;
}

bool inventory_clear_slot(int index) {
    if (index < 0 || index >= INV_SLOTS || inv_slots[index] < 0) {
        return false;
    }
    inv_slots[index] = -1;
    return true;
}

int inventory_count(void) {
    int count = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        if (inv_slots[i] >= 0) {
            count++;
        }
    }
    return count;
}

int inventory_slot(int index) {
    if (index < 0 || index >= INV_SLOTS) {
        return -1;
    }
    return inv_slots[index];
}

bool inventory_has(int item_type) {
    for (int i = 0; i < INV_SLOTS; i++) {
        if (inv_slots[i] == item_type) {
            return true;
        }
    }
    return false;
}

void inventory_set(int count, const int* slots) {
    if (count < 0) {
        count = 0;
    }
    if (count > INV_SLOTS) {
        count = INV_SLOTS;
    }
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slots[i] = (i < count && slots) ? slots[i] : -1;
    }
}
