#pragma once
#include <stdbool.h>

int crafting_count(void);

/* part < 0 reads the output. Absent is item -1 and qty 0. */
int crafting_part_item(int index, int part);
int crafting_part_qty(int index, int part);

bool crafting_ready(int index);
bool craft(int index);

bool crafting_known(int index);
bool crafting_learn(int index);
unsigned int crafting_known_mask(void);
void crafting_set_known_mask(unsigned int mask);
