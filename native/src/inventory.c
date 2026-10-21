#include "inventory.h"
#include "level.h"

// Fixed cells, so a move can leave an empty slot in the middle.
static int inv_slots[INV_SLOTS];
static int inv_capacity = INV_BASE_SLOTS;
// Key items and tools ride on a separate key ring, as in RE: they never
// take a slot, so progression can't be blocked by a full bag.
static int key_ring[INV_KEY_RING];
static int key_count;
static int box[INV_BOX_MAX];
static int box_count;

bool inventory_is_key_item(int item_type) {
    item_cat_t c = item_category(item_type);
    return c == ICAT_TOOL ||
           (c == ICAT_KEY && item_type != ITEM_COIN && item_type != ITEM_INK_RIBBON && item_type != ITEM_LOCKPICK);
}

void inventory_init(void) {
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slots[i] = -1;
    }
    key_count = 0;
    box_count = 0;
}

int inventory_key_count(void) { return key_count; }
int inventory_key(int index) { return index >= 0 && index < key_count ? key_ring[index] : -1; }

void inventory_set_keys(int count, const int* keys) {
    key_count = count < 0 ? 0 : count > INV_KEY_RING ? INV_KEY_RING : count;
    for (int i = 0; i < key_count; i++) key_ring[i] = keys[i];
}

static bool key_remove(int item_type) {
    for (int i = 0; i < key_count; i++) {
        if (key_ring[i] == item_type) {
            key_ring[i] = key_ring[--key_count];
            return true;
        }
    }
    return false;
}

int inventory_capacity(void) { return inv_capacity; }
bool inventory_expand(int by) {
    if (by <= 0 || inv_capacity >= INV_SLOTS) return false;
    inv_capacity += by; if (inv_capacity > INV_SLOTS) inv_capacity = INV_SLOTS;
    return true;
}
void inventory_set_capacity(int cap) {
    inv_capacity = cap < INV_BASE_SLOTS ? INV_BASE_SLOTS : cap > INV_SLOTS ? INV_SLOTS : cap;
}
bool inventory_add(int item_type) {
    if (inventory_is_key_item(item_type)) {
        if (key_count >= INV_KEY_RING) return false;
        key_ring[key_count++] = item_type;
        return true;
    }
    for (int i = 0; i < inv_capacity; i++) {
        if (inv_slots[i] < 0) {
            inv_slots[i] = item_type;
            return true;
        }
    }
    return false;
}

bool inventory_remove(int item_type) {
    if (key_remove(item_type)) return true;
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
    return inventory_count_of(item_type) > 0;
}

int inventory_count_of(int item_type) {
    if (item_type < 0) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < key_count; i++) {
        count += key_ring[i] == item_type;
    }
    for (int i = 0; i < INV_SLOTS; i++) {
        if (inv_slots[i] == item_type) {
            count++;
        }
    }
    return count;
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

int inventory_box_count(void) { return box_count; }
int inventory_box_item(int index) { return index >= 0 && index < box_count ? box[index] : -1; }

bool inventory_box_store(int slot) {
    if (slot < 0 || slot >= inv_capacity || inv_slots[slot] < 0 || box_count >= INV_BOX_MAX) return false;
    box[box_count++] = inv_slots[slot];
    inv_slots[slot] = -1;
    return true;
}

bool inventory_box_take(int index) {
    if (index < 0 || index >= box_count) return false;
    for (int i = 0; i < inv_capacity; i++) {
        if (inv_slots[i] < 0) {
            inv_slots[i] = box[index];
            for (int k = index; k + 1 < box_count; k++) box[k] = box[k + 1];
            box_count--;
            return true;
        }
    }
    return false;  // bag full
}

void inventory_box_set(int count, const int* items) {
    box_count = count < 0 ? 0 : count > INV_BOX_MAX ? INV_BOX_MAX : count;
    for (int i = 0; i < box_count; i++) box[i] = items[i];
}
