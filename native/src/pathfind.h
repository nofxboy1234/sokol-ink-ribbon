#pragma once
#include <stdbool.h>

#define PATH_MAX 4096

typedef struct {
    int count;
    int x[PATH_MAX];
    int y[PATH_MAX];
} path_t;

bool pathfind(int sx, int sy, int tx, int ty, path_t* out);
// Gives up on routes longer than max_cost steps, so short searches stay cheap.
bool pathfind_within(int sx, int sy, int tx, int ty, int max_cost, path_t* out);
