#include "throw.h"
#include "enemy.h"
#include "grid.h"
#include "inventory.h"
#include "level.h"
#include "sight.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static struct {
    bool equipped;
    bool active;
    unsigned int rng;
} t;

static unsigned int rnd(void) {
    t.rng ^= t.rng << 13;
    t.rng ^= t.rng >> 17;
    t.rng ^= t.rng << 5;
    return t.rng;
}

void throw_init(void) {
    t.equipped = false;
    t.active = false;
    if (t.rng == 0u) {
        t.rng = (unsigned int)time(NULL) * 2246822519u + 7u;
    }
}

void throw_seed(unsigned int seed) {
    t.rng = seed ? seed : 1u;
}

bool throw_equipped(void) {
    return t.equipped;
}

bool throw_active(void) {
    return t.active;
}

bool throw_set_equipped(bool on) {
    if (on && !inventory_has(ITEM_BOTTLE)) {
        return false;
    }
    t.equipped = on;
    if (!on) {
        t.active = false;
    }
    return true;
}

bool throw_set_active(bool on) {
    if (on && !t.equipped) {
        return false;
    }
    t.active = on;
    return true;
}

void throw_sync(void) {
    if (!inventory_has(ITEM_BOTTLE)) {
        t.equipped = false;
        t.active = false;
    }
}

bool throw_allowed(throw_stance_t stance) {
    return stance != THROW_STANCE_RUN;
}

int throw_hit_chance(throw_stance_t stance) {
    return stance == THROW_STANCE_WAIT ? THROW_HIT_WAIT_PCT
         : stance == THROW_STANCE_WALK ? THROW_HIT_WALK_PCT
                                       : 0;
}

bool throw_in_reach(int px, int py, int tx, int ty) {
    if ((px == tx && py == ty) || !grid_is_floor(tx, ty)) {
        return false;
    }
    float dx = (float)(tx - px);
    float dy = (float)(ty - py);
    if (dx * dx + dy * dy > THROW_RANGE_CELLS * THROW_RANGE_CELLS) {
        return false;
    }
    return sight_clear(px, py, tx, ty);
}

bool throw_aims_at_enemy(int tx, int ty) {
    int ex = (int)floorf(enemy_x());
    int ey = (int)floorf(enemy_y());
    return abs(ex - tx) <= THROW_ENEMY_REACH && abs(ey - ty) <= THROW_ENEMY_REACH;
}

bool throw_resolve(int px, int py, int tx, int ty, throw_stance_t stance, throw_result_t* out) {
    throw_result_t r = { 0 };
    if (!t.active || !throw_allowed(stance) || !throw_in_reach(px, py, tx, ty) ||
        !inventory_remove(ITEM_BOTTLE)) {
        return false;
    }
    throw_sync();
    r.aimed = throw_aims_at_enemy(tx, ty);
    r.hit = r.aimed && (int)(rnd() % 100u) < throw_hit_chance(stance);
    if (r.hit) {
        enemy_damage(THROW_STUN_SECONDS);
    } else {
        r.heard = enemy_hear_noise(tx, ty, THROW_NOISE_CELLS);
    }
    if (out) {
        *out = r;
    }
    return true;
}
