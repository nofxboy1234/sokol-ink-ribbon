#include "health.h"

static int state;

void health_init(void) {
    state = 0;
}

int health_state(void) {
    return state;
}

void health_set(int value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 2) {
        value = 2;
    }
    state = value;
}
