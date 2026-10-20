#pragma once
// Map streaming: the map is cut into STREAM_CHUNK x STREAM_CHUNK cell chunks.
// A chunk's render geometry is "resident" (built and cached) while it lies
// within the camera view plus STREAM_LOAD_MARGIN cells, and freed once it is
// more than STREAM_EVICT_MARGIN cells outside the view (hysteresis, so a
// chunk on the edge doesn't thrash). Only resident chunks that intersect the
// view are drawn. Gameplay state never streams: this only owns render data.
//
// Pop-in rule: a chunk must already be resident when it first intersects the
// view. stream_acquire() counts a violation when it is not (and loads it so
// the frame is still right); the tests require zero.
#include <stdbool.h>

#define STREAM_CHUNK 16
// Loaded ring around the view. Per frame the follow camera moves well under a
// cell (run speed 6.5 cells/s), so 8 cells is ~1 s of running at 10 fps;
// teleports (stairs, respawn) prefetch the destination view first.
#define STREAM_LOAD_MARGIN 8
#define STREAM_EVICT_MARGIN 24

typedef struct {
    float x0, y0, x1, y1;  // cells
} stream_rect_t;

// build: make the chunk's render data (*data, *bytes); free: release it.
typedef void (*stream_build_fn)(int cx, int cy, void** data, int* bytes);
typedef void (*stream_free_fn)(void* data);

typedef struct {
    int resident;     // chunks loaded now
    int peak_resident;
    long bytes;       // bytes of cached render data now
    long peak_bytes;
    long loads, evictions, rebuilds;
    long violations;  // chunks drawn before they were loaded (pop-in)
} stream_stats_t;

void stream_init(int cols, int rows, stream_build_fn build, stream_free_fn release);
void stream_shutdown(void);
int stream_chunk_cols(void);
int stream_chunk_rows(void);
// The map changed (reveal, doors...): resident chunks rebuild before drawing.
void stream_invalidate(void);
// Make resident everything within view + LOAD margin, evict past EVICT margin.
void stream_prefetch(stream_rect_t view);
// A chunk about to be drawn: its data, loading it now if it was not resident
// (that is a violation).
void* stream_acquire(int cx, int cy);
bool stream_is_resident(int cx, int cy);
// A resident chunk's data without loading anything (debug views).
void* stream_peek(int cx, int cy);
// Chunks that intersect a rect (cells), clamped to the map.
void stream_chunks_in(stream_rect_t r, int* cx0, int* cy0, int* cx1, int* cy1);
const stream_stats_t* stream_stats(void);
void stream_reset_stats(void);
