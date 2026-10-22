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
bool doors_has_key(int item);
bool doors_unlockable(const obj_t* o, int px, int py);
int doors_target_at(int x, int y);
void doors_unlock(int index);
// Removes one key once no locked door or closed openable still takes it.
void doors_drop_key_if_done(int item_type);
// Grace has been near it (or worked it): only then does it show its lock
// colour; until then every door draws grey, whatever its state.
bool doors_is_discovered(int index);
void doors_export_discovered(unsigned char* out);
void doors_import_discovered(const unsigned char* seen);
int doors_discovered_count(void);

int doors_pending_code(void);
bool doors_try_code(int code);
