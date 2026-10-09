#pragma once
#include <stdbool.h>

void interact_init(void);

// Interact with whatever sits on the given cell (switch, typewriter, file,
// safe). Returns true when something happened.
bool interact_at(int x, int y);

bool obstacles_block_cell(int x, int y);

bool lighter_on(void);
void lighter_toggle(void);

int files_found(void);
const char* file_name_at(int index);
int file_code_at(int index);

int saves_made(void);
