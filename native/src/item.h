#pragma once
#include <stdbool.h>
#include "vec2.h"

#define ITEM_GREEN_HERB 0
#define ITEM_COUNT 1

void items_init(void);

int item_count(void);
bool item_present(int item);

// cell position and map-unit center
int item_cell_x(int item);
int item_cell_y(int item);
vec2_t item_position(int item);

// true when Grace stands orthogonally next to the item's cell
bool item_in_reach(int item);

// index of a present item that Grace is next to, or -1
int item_any_in_reach(void);

// true when the pickup would actually succeed: in reach and a free slot
bool item_can_take(int item);

// pick the item up into the inventory, returns false when it is out of
// reach or the inventory is full (the item then stays where it is)
bool item_take(int item);