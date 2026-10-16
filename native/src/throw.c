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
    bool had;  // a bottle was in the inventory at the last sync
    unsigned int rng;
} t;

static bool throw_finish(throw_result_t* rp, throw_stance_t stance, int tx, int ty, throw_result_t* out);

static unsigned int rnd(void) {
    t.rng ^= t.rng << 13;
    t.rng ^= t.rng >> 17;
    t.rng ^= t.rng << 5;
    return t.rng;
}

void throw_init(void) {
    t.equipped = false;
    t.active = false;
    t.had = false;
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
    t.had = inventory_has(ITEM_BOTTLE);
    if (!on) {
        t.active = false;
    }
    return true;
}

void throw_restore(bool equipped, bool active) {
    t.had = inventory_has(ITEM_BOTTLE);
    t.equipped = equipped;
    t.active = equipped && active && t.had;
}

bool throw_empty(void) {
    return t.equipped && !inventory_has(ITEM_BOTTLE);
}

bool throw_set_active(bool on) {
    if (on && (!t.equipped || !inventory_has(ITEM_BOTTLE))) {
        return false;
    }
    t.active = on;
    return true;
}

// The slot outlives the bottles: with none left it stays equipped (greyed
// out, inactive). When she finds another it is ready in the same slot and
// becomes the active weapon unless another weapon already is (the bottle is
// the only weapon so far, so it always does).
bool throw_sync(void) {
    bool has = inventory_has(ITEM_BOTTLE);
    bool changed = false;
    if (!has && t.active) {
        t.active = false;
        changed = true;
    }
    if (has && !t.had && t.equipped && !t.active && !throw_other_weapon_active()) {
        t.active = true;
        changed = true;
    }
    if (has != t.had) {
        changed = true;
    }
    t.had = has;
    return changed;
}

bool throw_other_weapon_active(void) {
    return false;
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
    return throw_finish(&r, stance, tx, ty, out);
}

bool throw_resolve_at_enemy(int px, int py, int tx, int ty, throw_stance_t stance, throw_result_t* out) {
    throw_result_t r = { 0 };
    if (!t.active || !throw_allowed(stance) || !throw_in_reach(px, py, tx, ty) ||
        !inventory_remove(ITEM_BOTTLE)) {
        return false;
    }
    throw_sync();
    r.aimed = true;
    return throw_finish(&r, stance, tx, ty, out);
}

static bool throw_finish(throw_result_t* rp, throw_stance_t stance, int tx, int ty, throw_result_t* out) {
    throw_result_t r = *rp;
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
