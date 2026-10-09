#pragma once
#include <stdbool.h>

#define PATH_MAX 4096

typedef struct {
    int count;
    int x[PATH_MAX];
    int y[PATH_MAX];
} path_t;

bool pathfind(int sx, int sy, int tx, int ty, path_t* out);
