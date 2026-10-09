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

static void fill_floor_from_sections(const level_t* lv) {
    for (int s = 0; s < lv->section_count; s++) {
        const section_t* sec = &lv->sections[s];
        for (int y = sec->y; y < sec->y + sec->h; y++) {
            for (int x = sec->x; x < sec->x + sec->w; x++) {
                if (x >= 0 && x < g.cols && y >= 0 && y < g.rows) {
                    g.floor[y * g.cols + x] = 1;
                }
            }
        }
    }
}

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

void grid_init(const level_t* lv) {
    memset(&g, 0, sizeof(g));
    g.level = lv;
    g.cols = lv->cols < GRID_MAX_COLS ? lv->cols : GRID_MAX_COLS;
    g.rows = lv->rows < GRID_MAX_ROWS ? lv->rows : GRID_MAX_ROWS;
    fill_floor_from_sections(lv);
    rasterize_walls(lv);
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

int grid_revealed_count(void) {
    int n = 0;
    for (int i = 0; i < g.cols * g.rows; i++) {
        n += g.revealed[i] ? 1 : 0;
    }
    return n;
}
