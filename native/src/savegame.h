#pragma once
#include <stdbool.h>

#define SAVE_SLOTS 6

void savegame_init(void);
bool savegame_used(int slot);
double savegame_time(int slot);
bool savegame_save(int slot, double elapsed_ms);
bool savegame_load(int slot, double* elapsed_ms);
