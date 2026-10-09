#include "pathfind.h"
#include "doors.h"
#include "grid.h"

#include <limits.h>
#include <string.h>

#define PF_MAX (GRID_MAX_COLS * GRID_MAX_ROWS)

static struct {
    int g[PF_MAX];
    int f[PF_MAX];
    int came[PF_MAX];
    bool open[PF_MAX];
    bool closed[PF_MAX];
} pf;

static int heur(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx + dy;
}

bool pathfind(int sx, int sy, int tx, int ty, path_t* out) {
    out->count = 0;
    if (!grid_is_floor(sx, sy) || !grid_is_floor(tx, ty)) {
        return false;
    }
    int cols = grid_cols();
    int rows = grid_rows();
    int n = cols * rows;
    for (int i = 0; i < n; i++) {
        pf.g[i] = INT_MAX;
        pf.f[i] = INT_MAX;
        pf.came[i] = -1;
        pf.open[i] = false;
        pf.closed[i] = false;
    }
    int start = sy * cols + sx;
    int goal = ty * cols + tx;
    pf.g[start] = 0;
    pf.f[start] = heur(sx, sy, tx, ty);
    pf.open[start] = true;

    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };

    while (true) {
        int current = -1;
        int best = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (pf.open[i] && pf.f[i] < best) {
                best = pf.f[i];
                current = i;
            }
        }
        if (current < 0) {
            return false;
        }
        if (current == goal) {
            break;
        }
        pf.open[current] = false;
        pf.closed[current] = true;
        int cx = current % cols;
        int cy = current / cols;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (!grid_is_floor(nx, ny) || grid_blocked(cx, cy, nx, ny) || doors_block_cell(nx, ny)) {
                continue;
            }
            int ni = ny * cols + nx;
            if (pf.closed[ni]) {
                continue;
            }
            int tentative = pf.g[current] + 1;
            if (tentative < pf.g[ni]) {
                pf.came[ni] = current;
                pf.g[ni] = tentative;
                pf.f[ni] = tentative + heur(nx, ny, tx, ty);
                pf.open[ni] = true;
            }
        }
    }

    int cells[PATH_MAX];
    int count = 0;
    int node = goal;
    while (node != -1 && count < PATH_MAX) {
        cells[count++] = node;
        node = pf.came[node];
    }
    for (int i = count - 1; i >= 0 && out->count < PATH_MAX; i--) {
        int idx = cells[i];
        out->x[out->count] = idx % cols;
        out->y[out->count] = idx / cols;
        out->count++;
    }
    return out->count > 0;
}
