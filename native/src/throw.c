#include "throw.h"
#include "gear.h"
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
    if (on) {
        gear_unequip();  // one weapon in the East slot
    }
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
    if (on) {
        gear_set_active(false);
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
    return gear_active();
}

bool throw_allowed(throw_stance_t stance) {
    return stance != THROW_STANCE_RUN;
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
    // Hitscan is authoritative: "aimed" here means the ray geometrically
    // reached the enemy (caller checked line of sight), so it hits. The
    // stance is kept only as the Run guard in throw_allowed.
    (void)stance;
    throw_result_t r = *rp;
    r.hit = r.aimed;
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

bool throw_hitscan(float gx, float gy, float dx, float dy, float max_dist, throw_scan_t* out) {
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f || max_dist <= 0.0f) {
        return false;
    }
    dx /= len;
    dy /= len;
    // where the ray enters the enemy's circle, if it does at all
    float ex = enemy_x() - gx, ey = enemy_y() - gy;
    float proj = ex * dx + ey * dy;
    float perp2 = ex * ex + ey * ey - proj * proj;
    float r2 = THROW_ENEMY_RADIUS * THROW_ENEMY_RADIUS;
    float enemy_enter = -1.0f;
    if (proj > 0.0f && perp2 < r2) {
        enemy_enter = proj - sqrtf(r2 - perp2);
        if (enemy_enter < 0.0f) {
            enemy_enter = 0.0f;
        }
    }
    // walk the ray in small steps; the first blocker cell/edge wins
    int start_x = (int)floorf(gx), start_y = (int)floorf(gy);
    int prev_x = start_x, prev_y = start_y;
    float blocked_at = max_dist;
    bool blocked = false;
    const float step = 0.05f;
    for (float d = step; d <= max_dist; d += step) {
        float x = gx + dx * d, y = gy + dy * d;
        int cx = (int)floorf(x), cy = (int)floorf(y);
        if (cx != prev_x || cy != prev_y) {
            if (sight_edge_blocked(prev_x, prev_y, cx, cy)) {
                blocked_at = d;
                blocked = true;
                break;
            }
            prev_x = cx;
            prev_y = cy;
        }
        if (!grid_is_floor(cx, cy)) {
            blocked_at = d;
            blocked = true;
            break;
        }
        if ((cx != start_x || cy != start_y) && sight_cell_obstructed(cx, cy)) {
            blocked_at = d;
            blocked = true;
            break;
        }
        if (enemy_enter >= 0.0f && d >= enemy_enter) {
            break;  // reached the enemy before any blocker
        }
    }
    throw_scan_t r = { 0 };
    if (enemy_enter >= 0.0f && enemy_enter <= blocked_at && enemy_enter <= max_dist) {
        r.hit_enemy = true;
        r.dist = enemy_enter;
        r.x = enemy_x();
        r.y = enemy_y();
    } else {
        r.blocked = blocked;
        r.dist = blocked ? blocked_at : max_dist;
        r.x = gx + dx * r.dist;
        r.y = gy + dy * r.dist;
    }
    if (out) {
        *out = r;
    }
    return true;
}

bool throw_resolve_ray(float gx, float gy, float dx, float dy, float max_dist,
                       throw_stance_t stance, throw_result_t* out, float* impact_x, float* impact_y) {
    if (!t.active || !throw_allowed(stance)) {
        return false;
    }
    throw_scan_t scan;
    if (!throw_hitscan(gx, gy, dx, dy, max_dist, &scan)) {
        return false;
    }
    if (!inventory_remove(ITEM_BOTTLE)) {
        return false;
    }
    throw_sync();
    // the smash point: a hair back along the ray so it lands in a real cell
    float ix = scan.x, iy = scan.y;
    if (scan.blocked) {
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.001f) {
            ix -= dx / len * 0.06f;
            iy -= dy / len * 0.06f;
        }
    }
    if (impact_x) {
        *impact_x = ix;
    }
    if (impact_y) {
        *impact_y = iy;
    }
    throw_result_t r = { 0 };
    r.aimed = scan.hit_enemy;
    return throw_finish(&r, stance, (int)floorf(ix), (int)floorf(iy), out);
}
