#include "inventory.h"

// -1 marks an empty slot, otherwise the item id it holds
static int slots[INVENTORY_SLOTS];

void inventory_init(void) {
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        slots[i] = -1;
    }
}

int inventory_count(void) {
    int used = 0;
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        if (slots[i] >= 0) {
            used++;
        }
    }
    return used;
}

bool inventory_full(void) {
    return inventory_count() == INVENTORY_SLOTS;
}

int inventory_slot(int slot) {
    if ((slot < 0) || (slot >= INVENTORY_SLOTS)) {
        return -1;
    }
    return slots[slot];
}

// slots are kept in fill order, so the lowest free index is the
// top-left-most empty cell
bool inventory_add(int item) {
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        if (slots[i] < 0) {
            slots[i] = item;
            return true;
        }
    }
    return false;
}
