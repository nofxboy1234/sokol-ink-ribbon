#pragma once
#include <stdbool.h>

void items_init(void);
bool items_take(int object_index);
bool items_taken(int object_index);
bool items_taken_by_id(int id);
int items_taken_count(void);
void items_export(unsigned char* out, int cap);
void items_import(const unsigned char* in, int cap);
