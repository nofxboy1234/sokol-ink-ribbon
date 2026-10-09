#pragma once
#include <stdbool.h>

void doors_init(void);
bool doors_update(float dt);
bool doors_block_cell(int x, int y);
bool doors_interact(int x, int y);
int doors_discovered_count(void);
