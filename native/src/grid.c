#include "grid.h"

#include <string.h>

static struct {
    const level_t* level;
    int cols, rows;
    uint8_t floor[GRID_MAX_COLS * GRID_MAX_ROWS];
    uint8_t revealed[GRID_MAX_COLS * GRID_MAX_ROWS];
    // horizontal edges: (rows+1) * cols, wall on the line y=cy above cell cy
    uint8_t hwall[(GRID_MAX_ROWS + 1) * GRID_MAX_COLS];
    // vertical edges: rows * (cols+1), wall on the line x=cx left of cell cx
    uint8_t vwall[GRID_MAX_ROWS * (GRID_MAX_COLS + 1)];
} g;

static void rasterize_walls(const level_t* lv) {
    for (int i = 0; i < lv->wall_count; i++) {
        const wall_seg_t* w = &lv->walls[i];
        if (w->y0 == w->y1) {
            int y = w->y0;
            int x0 = w->x0 < w->x1 ? w->x0 : w->x1;
            int x1 = w->x0 < w->x1 ? w->x1 : w->x0;
            for (int x = x0; x < x1; x++) {
                if (y >= 0 && y <= g.rows && x >= 0 && x < g.cols) {
                    g.hwall[y * g.cols + x] = 1;
                }
            }
        } else if (w->x0 == w->x1) {
            int x = w->x0;
            int y0 = w->y0 < w->y1 ? w->y0 : w->y1;
            int y1 = w->y0 < w->y1 ? w->y1 : w->y0;
            for (int y = y0; y < y1; y++) {
                if (x >= 0 && x <= g.cols && y >= 0 && y < g.rows) {
                    g.vwall[y * (g.cols + 1) + x] = 1;
                }
            }
        }
    }
}

// The walkable interior is whatever is reachable from the start cell without
// crossing a wall, so the floor always sits exactly inside the drawn walls.
static void flood_floor(const level_t* lv) {
    int cols = g.cols;
    int rows = g.rows;
    if (lv->start_x < 0 || lv->start_x >= cols || lv->start_y < 0 || lv->start_y >= rows) {
        return;
    }
    static int stack[GRID_MAX_COLS * GRID_MAX_ROWS];
    int top = 0;
    int start = lv->start_y * cols + lv->start_x;
    g.floor[start] = 1;
    stack[top++] = start;
    while (top > 0) {
        int cur = stack[--top];
        int cx = cur % cols;
        int cy = cur / cols;
        int nx[4] = { cx + 1, cx - 1, cx, cx };
        int ny[4] = { cy, cy, cy + 1, cy - 1 };
        bool blocked[4] = {
            g.vwall[cy * (cols + 1) + (cx + 1)],
            g.vwall[cy * (cols + 1) + cx],
            g.hwall[(cy + 1) * cols + cx],
            g.hwall[cy * cols + cx],
        };
        for (int d = 0; d < 4; d++) {
            if (nx[d] < 0 || nx[d] >= cols || ny[d] < 0 || ny[d] >= rows || blocked[d]) {
                continue;
            }
            int ni = ny[d] * cols + nx[d];
            if (!g.floor[ni]) {
                g.floor[ni] = 1;
                stack[top++] = ni;
            }
        }
    }
}

// A door sits on a wall line; that piece of the wall must not block the floor,
// so the door's own state (locked/unlocked) decides passage instead.
static void clear_door_edges(const level_t* lv) {
    for (int i = 0; i < lv->obj_count; i++) {
        const obj_t* o = &lv->objs[i];
        if (o->kind != OBJ_DOOR) {
            continue;
        }
        int span = o->span > 0 ? o->span : 1;
        if (o->horizontal) {
            int y = o->y;
            for (int k = 0; k < span; k++) {
                int x = o->x + k;
                if (y >= 0 && y <= g.rows && x >= 0 && x < g.cols) {
                    g.hwall[y * g.cols + x] = 0;
                }
            }
        } else {
            int x = o->x;
            for (int k = 0; k < span; k++) {
                int y = o->y + k;
                if (x >= 0 && x <= g.cols && y >= 0 && y < g.rows) {
                    g.vwall[y * (g.cols + 1) + x] = 0;
                }
            }
        }
    }
}

void grid_init(const level_t* lv) {
    memset(&g, 0, sizeof(g));
    g.level = lv;
    g.cols = lv->cols < GRID_MAX_COLS ? lv->cols : GRID_MAX_COLS;
    g.rows = lv->rows < GRID_MAX_ROWS ? lv->rows : GRID_MAX_ROWS;
    rasterize_walls(lv);
    clear_door_edges(lv);
    flood_floor(lv);
}

int grid_cols(void) {
    return g.cols;
}

int grid_rows(void) {
    return g.rows;
}

const level_t* grid_level(void) {
    return g.level;
}

bool grid_in_bounds(int x, int y) {
    return x >= 0 && x < g.cols && y >= 0 && y < g.rows;
}

bool grid_is_floor(int x, int y) {
    return grid_in_bounds(x, y) && g.floor[y * g.cols + x];
}

bool grid_is_revealed(int x, int y) {
    return grid_in_bounds(x, y) && g.revealed[y * g.cols + x];
}

bool grid_blocked(int ax, int ay, int bx, int by) {
    if (bx == ax + 1 && by == ay) {
        return g.vwall[ay * (g.cols + 1) + (ax + 1)] != 0;
    }
    if (bx == ax - 1 && by == ay) {
        return g.vwall[ay * (g.cols + 1) + ax] != 0;
    }
    if (by == ay + 1 && bx == ax) {
        return g.hwall[(ay + 1) * g.cols + ax] != 0;
    }
    if (by == ay - 1 && bx == ax) {
        return g.hwall[ay * g.cols + ax] != 0;
    }
    return true;
}

void grid_reveal_around(int x, int y, int radius) {
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            int cx = x + dx;
            int cy = y + dy;
            if (grid_is_floor(cx, cy)) {
                g.revealed[cy * g.cols + cx] = 1;
            }
        }
    }
}

void grid_reveal_rect(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (grid_is_floor(i, j)) {
                g.revealed[j * g.cols + i] = 1;
            }
        }
    }
}

int grid_revealed_count(void) {
    int n = 0;
    for (int i = 0; i < g.cols * g.rows; i++) {
        n += g.revealed[i] ? 1 : 0;
    }
    return n;
}
