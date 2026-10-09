#include "inventory.h"

#include <string.h>

static struct {
    int count;
    int slots[INV_SLOTS];
} inv;

void inventory_init(void) {
    inv.count = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        inv.slots[i] = -1;
    }
}

bool inventory_add(int item_type) {
    if (inv.count >= INV_SLOTS) {
        return false;
    }
    inv.slots[inv.count++] = item_type;
    return true;
}

bool inventory_remove(int item_type) {
    for (int i = 0; i < inv.count; i++) {
        if (inv.slots[i] == item_type) {
            for (int j = i; j < inv.count - 1; j++) {
                inv.slots[j] = inv.slots[j + 1];
            }
            inv.count--;
            inv.slots[inv.count] = -1;
            return true;
        }
    }
    return false;
}

int inventory_count(void) {
    return inv.count;
}

int inventory_slot(int index) {
    if (index < 0 || index >= INV_SLOTS) {
        return -1;
    }
    return inv.slots[index];
}

bool inventory_has(int item_type) {
    for (int i = 0; i < inv.count; i++) {
        if (inv.slots[i] == item_type) {
            return true;
        }
    }
    return false;
}
