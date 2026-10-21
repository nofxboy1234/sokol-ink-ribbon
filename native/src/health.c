#include "health.h"

static int hp = HEALTH_BASE_MAX, max_hp = HEALTH_BASE_MAX;

void health_init(void) {
    hp = max_hp = HEALTH_BASE_MAX;
}

int health_hp(void) { return hp; }
int health_max(void) { return max_hp; }

int health_state(void) {
    if (hp >= max_hp) return 0;
    if (hp <= 1) return 2;
    return 1;
}

void health_set(int state) {
    if (state <= 0) hp = max_hp;
    else if (state == 1) hp = max_hp > 2 ? max_hp - 1 : max_hp;
    else hp = 1;
}

bool health_hurt(void) {
    if (--hp <= 0) {
        hp = 0;
        return true;
    }
    return false;
}

void health_heal(int points) {
    hp += points;
    if (hp > max_hp) hp = max_hp;
}

void health_heal_full(void) { hp = max_hp; }

void health_raise_max(int points) {
    max_hp += points;
    hp = max_hp;
}

void health_restore(int h, int m) {
    max_hp = m >= HEALTH_BASE_MAX ? m : HEALTH_BASE_MAX;
    hp = h > 0 && h <= max_hp ? h : max_hp;
}
