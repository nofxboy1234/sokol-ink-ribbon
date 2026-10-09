#pragma once
#include <stdbool.h>

void items_init(void);
bool items_update(void);
bool items_taken(int object_index);
int items_taken_count(void);
