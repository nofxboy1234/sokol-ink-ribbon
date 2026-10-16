#pragma once
#include <stdbool.h>

void items_init(void);
bool items_take(int object_index);
bool items_taken(int object_index);
bool items_taken_by_id(int id);
int items_taken_count(void);
void items_export(unsigned char* out, int cap);
void items_import(const unsigned char* in, int cap);
// Items seen in the Lighter's light stay visible on the map.
void items_mark_seen(int object_index);
bool items_seen(int object_index);
void items_export_seen(unsigned char* out, int cap);
void items_import_seen(const unsigned char* in, int cap);
