#include "pathfind.h"
#include <stdlib.h>

static int heuristic(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}

// pick the open node with the lowest f score, or -1 if the open set is empty
static int find_best_open(const float* f, const bool* open) {
    int best = -1;
    float best_f = 1e9f;
    for (int i = 0; i < PATH_MAX; i++) {
        if (open[i] && f[i] < best_f) {
            best_f = f[i];
            best = i;
        }
    }
    return best;
}

static void reconstruct(const int* parent, int goal, path_t* out) {
    out->len = 0;
    for (int c = goal; c != -1; c = parent[c]) {
        out->cells[out->len++] = c;
    }
    for (int i = 0; i < out->len / 2; i++) {
        const int tmp = out->cells[i];
        out->cells[i] = out->cells[out->len - 1 - i];
        out->cells[out->len - 1 - i] = tmp;
    }
}

bool pathfind_find(int sx, int sy, int tx, int ty, path_t* out) {
    static float g[PATH_MAX];
    static float f[PATH_MAX];
    static int parent[PATH_MAX];
    static bool open[PATH_MAX];
    static bool closed[PATH_MAX];
    for (int i = 0; i < PATH_MAX; i++) {
        g[i] = 1e9f;
        f[i] = 1e9f;
        parent[i] = -1;
        open[i] = false;
        closed[i] = false;
    }

    const int start = grid_cell_index(sx, sy);
    const int goal = grid_cell_index(tx, ty);
    g[start] = 0.0f;
    f[start] = (float)heuristic(sx, sy, tx, ty);
    open[start] = true;

    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };
    for (;;) {
        const int current = find_best_open(f, open);
        if (current < 0) {
            return false;
        }
        if (current == goal) {
            break;
        }
        open[current] = false;
        closed[current] = true;

        const int cx = current % GRID_W;
        const int cy = current / GRID_W;
        for (int k = 0; k < 4; k++) {
            const int nx = cx + dx[k];
            const int ny = cy + dy[k];
            if (!grid_is_floor(nx, ny) || grid_blocked(cx, cy, nx, ny)) {
                continue;
            }
            const int ni = grid_cell_index(nx, ny);
            if (closed[ni]) {
                continue;
            }
            const float ng = g[current] + 1.0f;
            if (ng < g[ni]) {
                g[ni] = ng;
                f[ni] = ng + (float)heuristic(nx, ny, tx, ty);
                parent[ni] = current;
                open[ni] = true;
            }
        }
    }

    reconstruct(parent, goal, out);
    return true;
}
