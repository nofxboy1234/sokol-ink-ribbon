#include "stream.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void* data;
    int bytes;
    bool resident;
    bool dirty;
} chunk_t;

static struct {
    chunk_t* c;
    int ccols, crows, cols, rows;
    stream_build_fn build;
    stream_free_fn release;
    stream_stats_t st;
} s;

static chunk_t* at(int cx, int cy) {
    return &s.c[cy * s.ccols + cx];
}

void stream_init(int cols, int rows, stream_build_fn build, stream_free_fn release) {
    stream_shutdown();
    s.cols = cols;
    s.rows = rows;
    s.ccols = (cols + STREAM_CHUNK - 1) / STREAM_CHUNK;
    s.crows = (rows + STREAM_CHUNK - 1) / STREAM_CHUNK;
    s.c = calloc((size_t)(s.ccols * s.crows), sizeof(chunk_t));
    s.build = build;
    s.release = release;
    memset(&s.st, 0, sizeof s.st);
}

static void unload(chunk_t* k) {
    if (k->resident) {
        if (s.release) {
            s.release(k->data);
        }
        s.st.bytes -= k->bytes;
        s.st.resident--;
        k->data = NULL;
        k->bytes = 0;
        k->resident = false;
    }
}

void stream_shutdown(void) {
    if (s.c) {
        for (int i = 0; i < s.ccols * s.crows; i++) {
            unload(&s.c[i]);
        }
        free(s.c);
    }
    s.c = NULL;
    s.ccols = s.crows = 0;
}

int stream_chunk_cols(void) {
    return s.ccols;
}

int stream_chunk_rows(void) {
    return s.crows;
}

static void load(int cx, int cy) {
    chunk_t* k = at(cx, cy);
    if (k->resident) {
        unload(k);  // a rebuild: drop the old data first
    } else {
        s.st.loads++;
    }
    void* data = NULL;
    int bytes = 0;
    if (s.build) {
        s.build(cx, cy, &data, &bytes);
    }
    k->data = data;
    k->bytes = bytes;
    k->resident = true;
    k->dirty = false;
    s.st.resident++;
    s.st.bytes += bytes;
    if (s.st.resident > s.st.peak_resident) {
        s.st.peak_resident = s.st.resident;
    }
    if (s.st.bytes > s.st.peak_bytes) {
        s.st.peak_bytes = s.st.bytes;
    }
}

void stream_invalidate(void) {
    for (int i = 0; i < s.ccols * s.crows; i++) {
        s.c[i].dirty = true;
    }
}

void stream_chunks_in(stream_rect_t r, int* cx0, int* cy0, int* cx1, int* cy1) {
    int a = (int)floorf(r.x0 / STREAM_CHUNK), b = (int)floorf(r.y0 / STREAM_CHUNK);
    int c = (int)floorf(r.x1 / STREAM_CHUNK), d = (int)floorf(r.y1 / STREAM_CHUNK);
    *cx0 = a < 0 ? 0 : a;
    *cy0 = b < 0 ? 0 : b;
    *cx1 = c >= s.ccols ? s.ccols - 1 : c;
    *cy1 = d >= s.crows ? s.crows - 1 : d;
}

static stream_rect_t grow(stream_rect_t r, float m) {
    return (stream_rect_t){ r.x0 - m, r.y0 - m, r.x1 + m, r.y1 + m };
}

void stream_prefetch(stream_rect_t view) {
    if (!s.c) {
        return;
    }
    int x0, y0, x1, y1;
    stream_chunks_in(grow(view, STREAM_LOAD_MARGIN), &x0, &y0, &x1, &y1);
    for (int cy = y0; cy <= y1; cy++) {
        for (int cx = x0; cx <= x1; cx++) {
            if (!at(cx, cy)->resident) {
                load(cx, cy);
            }
        }
    }
    int ex0, ey0, ex1, ey1;
    stream_chunks_in(grow(view, STREAM_EVICT_MARGIN), &ex0, &ey0, &ex1, &ey1);
    for (int cy = 0; cy < s.crows; cy++) {
        for (int cx = 0; cx < s.ccols; cx++) {
            chunk_t* k = at(cx, cy);
            if (k->resident && (cx < ex0 || cx > ex1 || cy < ey0 || cy > ey1)) {
                unload(k);
                s.st.evictions++;
            }
        }
    }
}

void* stream_acquire(int cx, int cy) {
    if (!s.c || cx < 0 || cy < 0 || cx >= s.ccols || cy >= s.crows) {
        return NULL;
    }
    chunk_t* k = at(cx, cy);
    if (!k->resident) {
        s.st.violations++;
        load(cx, cy);
    } else if (k->dirty) {
        s.st.rebuilds++;
        load(cx, cy);
    }
    return k->data;
}

void* stream_peek(int cx, int cy) {
    return stream_is_resident(cx, cy) ? at(cx, cy)->data : NULL;
}

bool stream_is_resident(int cx, int cy) {
    return s.c && cx >= 0 && cy >= 0 && cx < s.ccols && cy < s.crows && at(cx, cy)->resident;
}

const stream_stats_t* stream_stats(void) {
    return &s.st;
}

void stream_reset_stats(void) {
    int r = s.st.resident;
    long b = s.st.bytes;
    memset(&s.st, 0, sizeof s.st);
    s.st.resident = s.st.peak_resident = r;
    s.st.bytes = s.st.peak_bytes = b;
}
