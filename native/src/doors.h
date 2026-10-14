#pragma once
#include <stdbool.h>
#include "level.h"

void doors_init(void);
bool doors_update(float dt);
bool doors_on_edge(int ax, int ay, int bx, int by);
bool doors_block_edge(int ax, int ay, int bx, int by);
// through_breakable lets the enemy plan through doors it could smash.
bool doors_approach_blocked(int from_x, int from_y, int to_x, int to_y, bool through_breakable);
bool doors_block_sight(int ax, int ay, int bx, int by);
bool doors_breakable_edge(int ax, int ay, int bx, int by);
// Index of the closed, unbroken door on this edge, or -1.
int doors_closed_on_edge(int ax, int ay, int bx, int by);
void doors_open(int index);
void doors_break(int index);
bool doors_interact(int x, int y);
int doors_key_type(const obj_t* o);
// Removes one key once no locked door or closed openable still takes it.
void doors_drop_key_if_done(int item_type);
int doors_discovered_count(void);
void doors_restore_from_level(void);
