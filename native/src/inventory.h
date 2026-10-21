#pragma once
#include <stdbool.h>

#define INV_SLOTS 16
#define INV_BASE_SLOTS 8
#define INV_KEY_RING 64
#define INV_BOX_MAX 64

void inventory_init(void);
bool inventory_add(int item_type);
bool inventory_remove(int item_type);
bool inventory_move(int from, int to);
bool inventory_clear_slot(int index);
int inventory_count(void);
int inventory_slot(int index);
bool inventory_has(int item_type);
int inventory_count_of(int item_type);
void inventory_set(int count, const int* slots);

// Key items and tools: kept on a key ring, not in slots.
bool inventory_is_key_item(int item_type);
int inventory_key_count(void);
int inventory_key(int index);
void inventory_set_keys(int count, const int* keys);

int inventory_capacity(void);
bool inventory_expand(int by);
void inventory_set_capacity(int cap);

// Item Box (as in RE): slot items stored at any box, shared by every box.
int inventory_box_count(void);
int inventory_box_item(int index);
bool inventory_box_store(int slot);   // slot item -> box
bool inventory_box_take(int index);   // box item -> first free slot
void inventory_box_set(int count, const int* items);
