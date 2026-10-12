#pragma once
#include <stdbool.h>
#include "level.h"

void doors_init(void);
bool doors_update(float dt);
bool doors_on_edge(int ax, int ay, int bx, int by);
bool doors_block_edge(int ax, int ay, int bx, int by);
bool doors_approach_blocked(int from_x, int from_y, int to_x, int to_y);
bool doors_interact(int x, int y);
int doors_key_type(const obj_t* o);
// Removes one key once no locked door or closed openable still takes it.
void doors_drop_key_if_done(int item_type);
int doors_discovered_count(void);
void doors_restore_from_level(void);
