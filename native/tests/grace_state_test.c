// Every allowed and disallowed transition of Grace's state machine, and the
// return-to-previous rule for states that finish on their own.
#include <stdio.h>
#include "grace_state.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

static const char* N[GS_COUNT] = {"wait", "walk", "run", "throw", "hurt"};

int main(void) {
    // the diagram's edges (both directions where it has a double arrow)
    static const int edges[][2] = {
        {GS_WALK, GS_THROW}, {GS_THROW, GS_WALK}, {GS_THROW, GS_WAIT}, {GS_WAIT, GS_THROW},
        {GS_WALK, GS_RUN}, {GS_RUN, GS_WALK}, {GS_WALK, GS_WAIT}, {GS_WAIT, GS_WALK},
        {GS_RUN, GS_WAIT}, {GS_WAIT, GS_RUN}, {GS_WALK, GS_HURT}, {GS_RUN, GS_HURT},
        {GS_WAIT, GS_HURT}, {GS_HURT, GS_WAIT},
        // re-choosing the same action (a new target, waiting on)
        {GS_WAIT, GS_WAIT}, {GS_WALK, GS_WALK}, {GS_RUN, GS_RUN},
    };
    int n = sizeof edges / sizeof edges[0];
    char msg[64];
    for (int f = 0; f < GS_COUNT; f++) {
        for (int t = 0; t < GS_COUNT; t++) {
            int want = 0;
            for (int i = 0; i < n; i++) {
                want |= edges[i][0] == f && edges[i][1] == t;
            }
            snprintf(msg, sizeof msg, "%s -> %s %s", N[f], N[t], want ? "allowed" : "not allowed");
            CHECK(grace_state_allowed(f, t) == want, msg);
        }
    }
    CHECK(grace_state_finish(GS_WALK, GS_WAIT, false) == GS_WAIT, "walk arrives -> paused wait");
    CHECK(grace_state_finish(GS_RUN, GS_WAIT, false) == GS_WAIT, "run arrives -> paused wait");
    CHECK(grace_state_finish(GS_WALK, GS_WALK, false) == GS_WAIT, "walk after walk arrives -> wait");
    CHECK(grace_state_finish(GS_WALK, GS_RUN, false) == GS_WAIT, "walk after run, no route left -> wait");
    CHECK(grace_state_finish(GS_THROW, GS_WALK, true) == GS_WALK, "throw while walking -> paused walk");
    CHECK(grace_state_finish(GS_THROW, GS_WALK, false) == GS_WAIT, "throw, walk has no route -> wait");
    CHECK(grace_state_finish(GS_THROW, GS_WAIT, false) == GS_WAIT, "throw while waiting -> paused wait");
    CHECK(grace_state_finish(GS_THROW, GS_RUN, true) == GS_WAIT, "throw->run is no edge -> wait");
    CHECK(grace_state_finish(GS_HURT, GS_WALK, true) == GS_WAIT, "hurt -> wait");
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails != 0;
}
