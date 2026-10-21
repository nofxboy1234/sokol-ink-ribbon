#pragma once
#include <stdbool.h>

void interact_init(void);

// Interact with whatever sits on the given cell (switch, typewriter, file,
// safe). Returns true when something happened.
bool interact_at(int x, int y);

// The interact press: the cell she faces, her own, then the rest around her.
// On each cell an item she can carry comes first, then (unless items_only:
// she is moving) doors, switches and the rest. An item she can't take, or
// one already taken, never ends the search.
bool interact_nearby(bool items_only);

// True if Grace can actually reach the given cell to interact with it:
// her own cell, or an adjacent cell whose connecting edge is not blocked
// by a wall or a blocking (locked/unknown, shut) door -- diagonally, at
// least one of the two corner routes must be clear. Chebyshev distance
// alone let her pick items up through walls; this is the fix.
bool interact_cell_reachable(int x, int y);

bool obstacles_block_cell(int x, int y);

// Chebyshev distance from Grace to an object (support-aware for items).
int interact_distance(int object_index);

// True if a closed openable covers this cell (so its contents stay hidden).
bool openable_covers_closed(int x, int y);

bool lighter_on(void);
void lighter_toggle(void);

int files_found(void);
// True once she has picked up the file object at this index.
bool interact_file_found(int obj_index);
const char* file_name_at(int index);
int file_code_at(int index);

int saves_made(void);

bool goal_met(void);
int interact_pending_safe(void);
bool interact_try_safe_code(int code);
bool level_complete(void);
bool interact_check_complete(void);

// Save/load state.
bool interact_take_save_menu(void);
// True once after she opens an Item Box (the UI shows it).
bool interact_take_itembox(void);
void interact_export(bool* lighter, int* saves, bool* goal, int* file_count, int* file_obj, bool* complete);
void interact_restore(bool lighter, int saves, bool goal, int file_count, const int* file_obj, bool complete);

bool cell_on_climbable(int x, int y);
bool interact_climb_at(int x, int y);
