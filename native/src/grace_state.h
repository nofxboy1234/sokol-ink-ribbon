#pragma once
#include <stdbool.h>

// Grace's state machine: the single source of truth for which state may
// follow which (see grace_states.png). "Paused" is not a state here: it is a
// modifier on top of the current one (Paused Walking, Paused Wait, ...).
typedef enum {
    GS_WAIT = 0,
    GS_WALK,
    GS_RUN,
    GS_THROW,  // instant: finishes as soon as the bottle leaves her hand
    GS_HURT,   // 5 s, can't act, time runs
    GS_COUNT,
} grace_state_t;

// May she go from one state to another? Re-choosing the same Walk/Run/Wait
// (a new target, or waiting on) is allowed; Hurt and Throw never repeat.
bool grace_state_allowed(grace_state_t from, grace_state_t to);

// A state that finishes on its own (Walk/Run arriving, Throw done, Hurt over)
// goes back to the state she was in before it, and she pauses. resumable says
// whether that previous state can still carry on (a Walk/Run needs a route
// left). Falls back to Wait when the previous state is unavailable: the same
// state, not resumable, or not an allowed edge. Hurt always ends in Wait.
grace_state_t grace_state_finish(grace_state_t finished, grace_state_t previous, bool resumable);
