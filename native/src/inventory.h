#pragma once
#include <stdbool.h>
#include "webexport.h"

// Grace's bag: 4 columns x 2 rows of cells, filled from the top-left cell.
// The React app draws this grid to the right of the map.
#define INVENTORY_COLS 4
#define INVENTORY_ROWS 2
#define INVENTORY_SLOTS (INVENTORY_COLS * INVENTORY_ROWS)

void inventory_init(void);

// number of filled slots
int inventory_count(void);

// true when every slot is taken, i.e. no further item can be picked up
bool inventory_full(void);

// item id stored in the given slot (row-major, slot 0 is the top-left cell),
// or -1 when the slot is empty. Callable from JavaScript as _inventory_slot.
WEB_EXPORT int inventory_slot(int slot);

// store the item in the first free slot (top-left first),
// returns false when the inventory is full
bool inventory_add(int item);
