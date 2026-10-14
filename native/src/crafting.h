#pragma once
#include <stdbool.h>

int crafting_count(void);

/* part < 0 reads the output. Absent is item -1 and qty 0. */
int crafting_part_item(int index, int part);
int crafting_part_qty(int index, int part);

bool crafting_ready(int index);
bool craft(int index);
