#pragma once
#include <stdbool.h>

void doors_init(void);
bool doors_update(float dt);
bool doors_on_edge(int ax, int ay, int bx, int by);
bool doors_block_edge(int ax, int ay, int bx, int by);
bool doors_interact(int x, int y);
int doors_discovered_count(void);
