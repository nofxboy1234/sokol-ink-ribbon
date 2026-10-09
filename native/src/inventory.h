#pragma once
#include <stdbool.h>

#define INV_SLOTS 8

void inventory_init(void);
bool inventory_add(int item_type);
bool inventory_remove(int item_type);
int inventory_count(void);
int inventory_slot(int index);
bool inventory_has(int item_type);
