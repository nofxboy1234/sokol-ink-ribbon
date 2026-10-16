#include "grace_state.h"

// [from][to]
static const bool allowed[GS_COUNT][GS_COUNT] = {
    //              WAIT   WALK   RUN    THROW  HURT
    [GS_WAIT]  = { true,  true,  true,  true,  true  },
    [GS_WALK]  = { true,  true,  true,  true,  true  },
    [GS_RUN]   = { true,  true,  true,  false, true  },
    [GS_THROW] = { true,  true,  false, false, false },
    [GS_HURT]  = { true,  false, false, false, false },
};

bool grace_state_allowed(grace_state_t from, grace_state_t to) {
    if (from < 0 || from >= GS_COUNT || to < 0 || to >= GS_COUNT) {
        return false;
    }
    return allowed[from][to];
}

grace_state_t grace_state_finish(grace_state_t finished, grace_state_t previous, bool resumable) {
    if (finished == GS_HURT) {
        return GS_WAIT;
    }
    if (previous == finished || previous == GS_HURT || previous == GS_THROW ||
        !grace_state_allowed(finished, previous)) {
        return GS_WAIT;
    }
    if ((previous == GS_WALK || previous == GS_RUN) && !resumable) {
        return GS_WAIT;
    }
    return previous;
}
