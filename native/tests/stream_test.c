// Map streaming bookkeeping (src/stream.c): chunks load ahead of the view,
// evict with hysteresis, and nothing is drawn before it is loaded.
#include "../src/stream.h"

#include <stdio.h>
#include <stdlib.h>

static int fails;
static void check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    fails += !ok;
}

static int built, freed;
static void build(int cx, int cy, void** data, int* bytes) {
    (void)cx; (void)cy;
    *data = malloc(64);
    *bytes = 64;
    built++;
}
static void release(void* d) {
    free(d);
    freed++;
}

// one frame as the renderer does it: draw (acquire) what the view touches,
// then prefetch for the next frame
static void frame(stream_rect_t v) {
    int x0, y0, x1, y1;
    stream_chunks_in(v, &x0, &y0, &x1, &y1);
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) stream_acquire(cx, cy);
    stream_prefetch(v);
}

static stream_rect_t view(float cx, float cy) {
    return (stream_rect_t){ cx - 36, cy - 22, cx + 36, cy + 22 };  // a 72x44-cell window
}

int main(void) {
    stream_init(184, 252, build, release);
    check(stream_chunk_cols() == 12 && stream_chunk_rows() == 16, "stream: 184x252 cells is 12x16 chunks");
    stream_prefetch(view(159, 68));  // the start: loaded before the first frame
    // run at 6.5 cells/s, 10 fps (a slow frame), across the map and back
    float x = 159, y = 68;
    for (int i = 0; i < 400; i++) {
        x += (i < 200 ? -0.65f : 0.65f);
        y += (i % 100 < 50 ? 0.65f : -0.4f);
        frame(view(x, y));
    }
    check(stream_stats()->violations == 0, "stream: running about, every chunk is loaded before it is in view");
    check(stream_stats()->evictions > 0, "stream: chunks far behind are freed");
    check(stream_stats()->resident < 12 * 16, "stream: never the whole map resident");
    // a teleport with the destination prefetched first: no pop-in
    stream_prefetch(view(30, 230));
    frame(view(30, 230));
    check(stream_stats()->violations == 0, "stream: a prefetched teleport does not pop in");
    // the same without the prefetch is caught
    frame(view(170, 10));
    check(stream_stats()->violations > 0, "stream: a teleport without prefetch is counted as pop-in");
    // hysteresis: a chunk just past the load ring stays resident
    stream_prefetch(view(100, 100));
    stream_reset_stats();
    stream_prefetch(view(100 + 10, 100));
    stream_prefetch(view(100, 100));
    check(stream_stats()->evictions == 0, "stream: small moves back and forth do not evict (hysteresis)");
    // invalidate: resident chunks rebuild on draw, no violation
    stream_invalidate();
    frame(view(100, 100));
    check(stream_stats()->rebuilds > 0 && stream_stats()->violations == 0, "stream: a map change rebuilds resident chunks");
    stream_shutdown();
    check(built == freed, "stream: every built chunk is freed");
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
