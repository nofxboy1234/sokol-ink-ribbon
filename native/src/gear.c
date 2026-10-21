#include "gear.h"
#include "crafting.h"
#include "enemy.h"
#include "health.h"
#include "inventory.h"
#include "level.h"
#include "throw.h"

#include <math.h>
#include <string.h>

// Numbers from the RE Requiem stat tables, scaled to stun seconds: the
// enemy here can't die, so damage becomes how long it stays down.
static const weapon_t k_weapons[] = {
    { ITEM_GUN_M232, AMMO_HANDGUN, 10.0f, 5.0f, 12.0f, 1, 0, 0.0f, false },
    { ITEM_GUN_B934, AMMO_HANDGUN, 9.0f, 4.0f, 12.0f, 1, 0, 0.0f, false },
    { ITEM_GUN_REQUIEM, AMMO_MAGNUM, 14.0f, 20.0f, 16.0f, 1, 0, 0.0f, false },
    { ITEM_GUN_MSBG500, AMMO_SHELLS, 6.0f, 10.0f, 14.0f, 5, 0, 0.0f, false },
    { ITEM_HUNTING_KNIFE, -1, GEAR_MELEE_REACH, 2.5f, 2.0f, 1, 4, 0.0f, false },
    { ITEM_MAKESHIFT_KNIFE, -1, GEAR_MELEE_REACH, 2.5f, 2.0f, 1, 12, 0.0f, false },
    { ITEM_CHAINSAW, -1, GEAR_MELEE_REACH, 8.0f, 14.0f, 1, 6, 0.0f, false },
    { ITEM_HEMOLYTIC_INJECTOR, -1, GEAR_MELEE_REACH, 45.0f, 0.0f, 1, 1, 0.0f, true },
    { ITEM_HAND_GRENADE, -1, THROW_RANGE_CELLS, 6.0f, 16.0f, 1, 1, 2.0f, false },
};
#define WEAPON_COUNT ((int)(sizeof(k_weapons) / sizeof(k_weapons[0])))

static const float k_stabilizer_mult[GEAR_MAX_DOSES + 1] = { 1.0f, 1.1f, 1.15f, 1.2f, 1.25f };

static struct {
    int ammo[AMMO_KIND_COUNT];
    int blood, blood_cap;
    int scrap, rare_metal;
    int steroids, stabilizer;
    int grit;
    int collectibles;
    int equipped;  // item or -1
    int active;
    int wear;      // uses left on the melee weapon in hand
    int eye_spy_used;
    int stabs;     // knife hits, for the Stakeout Takeout Charm's free one in five
} g = { .blood_cap = GEAR_BLOOD_CAP, .equipped = -1 };

void gear_init(void) {
    memset(&g, 0, sizeof(g));
    g.blood_cap = GEAR_BLOOD_CAP;
    g.equipped = -1;
}

const weapon_t* gear_weapon(int item) {
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (k_weapons[i].item == item) return &k_weapons[i];
    }
    return 0;
}

static int ammo_kind_of(int item) {
    switch (item) {
        case ITEM_HANDGUN_AMMO: return AMMO_HANDGUN;
        case ITEM_MAGNUM_AMMO: return AMMO_MAGNUM;
        case ITEM_SHOTGUN_SHELLS: return AMMO_SHELLS;
        default: return -1;
    }
}

// The counter a counted item lives in, or 0 for slot items.
static int* counter_of(int item) {
    int a = ammo_kind_of(item);
    if (a >= 0) return &g.ammo[a];
    if (item == ITEM_SCRAP) return &g.scrap;
    if (item == ITEM_RARE_METAL) return &g.rare_metal;
    if (item == ITEM_INFECTED_BLOOD) return &g.blood;
    if (item == ITEM_MR_RACCOON) return &g.collectibles;
    return 0;
}

bool gear_counted(int item) {
    return counter_of(item) != 0;
}

int gear_count(int item) {
    int* c = counter_of(item);
    return c ? *c : inventory_count_of(item);
}

bool gear_spend(int item, int qty) {
    int* c = counter_of(item);
    if (c) {
        if (*c < qty) return false;
        *c -= qty;
        return true;
    }
    if (inventory_count_of(item) < qty) return false;
    for (int i = 0; i < qty; i++) inventory_remove(item);
    return true;
}

bool gear_give(int item, int qty) {
    if (qty < 1) qty = 1;
    int* c = counter_of(item);
    if (c) {
        *c += qty;
        if (item == ITEM_INFECTED_BLOOD && g.blood > g.blood_cap) g.blood = g.blood_cap;
        return true;
    }
    for (int i = 0; i < qty; i++) {
        if (!inventory_add(item)) return false;
    }
    return true;
}

bool gear_pickup(int item, int qty) {
    if (qty < 1) qty = 1;
    switch (item_category(item)) {
        case ICAT_BLOOD:
            // the Blood Collector draws it up; without one it stays put
            if (!inventory_has(ITEM_BLOOD_COLLECTOR) || g.blood >= g.blood_cap) return false;
            return gear_give(item, qty);
        case ICAT_POUCH:
            inventory_expand(2);
            return true;
        case ICAT_GUN: {
            // a gun found loaded: its rounds join the ammo count
            if (!inventory_add(item)) return false;
            const weapon_t* w = gear_weapon(item);
            if (w && w->ammo >= 0 && qty > 1) g.ammo[w->ammo] += qty;
            if (g.equipped < 0 && !throw_equipped()) gear_equip(item);
            return true;
        }
        default:
            if (counter_of(item)) return gear_give(item, qty);
            return inventory_add(item);
    }
}

int gear_ammo(int kind) { return kind >= 0 && kind < AMMO_KIND_COUNT ? g.ammo[kind] : 0; }
int gear_blood(void) { return g.blood; }
int gear_blood_cap(void) { return g.blood_cap; }
int gear_doses_steroids(void) { return g.steroids; }
int gear_doses_stabilizer(void) { return g.stabilizer; }
int gear_collectibles(void) { return g.collectibles; }
int gear_grit(void) { return g.grit; }

bool gear_use_slot(int index) {
    int item = inventory_slot(index);
    int health = health_state();
    switch (item) {
        case ITEM_HERB:
        case ITEM_MIXED_HERB:
        case ITEM_INJECTOR: {
            int heal = item == ITEM_HERB ? 1 : 2;
            if (health == 0) return false;
            if (!inventory_clear_slot(index)) return false;
            health_set(health - heal);
            if (health_state() == 0) gear_refill_grit();
            return true;
        }
        case ITEM_TRANSFUSION_BAG:
            if (!inventory_has(ITEM_BLOOD_COLLECTOR) || g.blood >= g.blood_cap) return false;
            inventory_clear_slot(index);
            g.blood += GEAR_TRANSFUSION_UNITS;
            if (g.blood > g.blood_cap) g.blood = g.blood_cap;  // the rest is lost
            return true;
        case ITEM_OVERRIDE_MANUAL:
            inventory_clear_slot(index);
            g.blood_cap += GEAR_BLOOD_CAP_STEP;
            return true;
        case ITEM_STEROIDS:
            if (g.steroids >= GEAR_MAX_DOSES) return false;
            inventory_clear_slot(index);
            g.steroids++;
            health_set(0);
            gear_refill_grit();
            return true;
        case ITEM_STABILIZER:
            if (g.stabilizer >= GEAR_MAX_DOSES) return false;
            inventory_clear_slot(index);
            g.stabilizer++;
            return true;
        default:
            return false;
    }
}

int gear_equipped(void) { return g.equipped; }

bool gear_equip(int item) {
    const weapon_t* w = gear_weapon(item);
    if (!w || !inventory_has(item)) return false;
    throw_set_equipped(false);  // one weapon in the East slot
    if (g.equipped != item) {
        g.wear = w->uses;
    }
    g.equipped = item;
    g.active = 0;
    return true;
}

void gear_unequip(void) {
    g.equipped = -1;
    g.active = 0;
}

bool gear_active(void) { return g.active && g.equipped >= 0; }

bool gear_ready(void) {
    if (g.equipped < 0 || !inventory_has(g.equipped)) return false;
    const weapon_t* w = gear_weapon(g.equipped);
    if (w->ammo >= 0) return g.ammo[w->ammo] > 0;
    return true;
}

bool gear_set_active(bool on) {
    if (on && !gear_ready()) return false;
    if (on) throw_set_active(false);
    g.active = on ? 1 : 0;
    return true;
}

float gear_range(void) {
    const weapon_t* w = gear_weapon(g.equipped);
    if (!w) return THROW_RANGE_CELLS;
    float r = w->range;
    if (w->ammo >= 0 && inventory_has(ITEM_COMPENSATOR)) r += 2.0f;
    return r;
}

static float stun_mult(const weapon_t* w) {
    if (w->ammo >= 0) {
        float m = k_stabilizer_mult[g.stabilizer];
        if (inventory_has(ITEM_CHARM_CONNOISSEUR)) m *= 1.1f;
        return m;
    }
    if (w->uses > 1 && w->item != ITEM_CHAINSAW && inventory_has(ITEM_CHARM_STAKEOUT)) return 1.5f;
    return 1.0f;
}

// A used-up melee weapon or throwable leaves her hand; the next one of the
// same kind takes its place.
static void wear_out(const weapon_t* w) {
    if (--g.wear > 0) return;
    inventory_remove(w->item);
    g.wear = w->uses;
    if (!inventory_has(w->item)) gear_unequip();
}

bool gear_fire(float gx, float gy, float dx, float dy, float len, gear_shot_t* out) {
    memset(out, 0, sizeof(*out));
    if (!gear_active() || !gear_ready()) return false;
    const weapon_t* w = gear_weapon(g.equipped);
    float range = gear_range();
    bool thrown = w->blast > 0.0f;
    float reach = thrown ? (len < range ? len : range) : range;
    if (w->needs_stun) {
        enemy_state_t s = enemy_state();
        float ex = enemy_x() - gx, ey = enemy_y() - gy;
        if ((s != ENEMY_STUNNED && s != ENEMY_PATROL) || ex * ex + ey * ey > range * range) return false;
    }
    float base = atan2f(dy, dx);
    for (int p = 0; p < w->pellets; p++) {
        // pellets fan out 8 degrees apart around the aim
        float a = base + ((float)p - (float)(w->pellets - 1) * 0.5f) * 0.14f;
        throw_scan_t scan;
        throw_hitscan(gx, gy, cosf(a), sinf(a), reach, &scan);
        if (p == 0 || scan.hit_enemy) {
            out->x = scan.x;
            out->y = scan.y;
        }
        if (scan.hit_enemy) out->hit = true;
    }
    if (thrown) {
        float ex = enemy_x() - out->x, ey = enemy_y() - out->y;
        out->hit = out->hit || ex * ex + ey * ey <= w->blast * w->blast;
    }
    if (out->hit) {
        enemy_damage(w->stun * stun_mult(w));
    }
    if (w->noise > 0.0f) {
        enemy_hear_noise((int)floorf(out->x), (int)floorf(out->y), w->noise);
    }
    if (w->ammo >= 0) {
        g.ammo[w->ammo]--;
        if (g.ammo[w->ammo] == 0) g.active = 0;
    } else if (out->hit || thrown) {
        // a knife only wears when it connects
        // the Stakeout Takeout Charm's 0.8x wear: every fifth knife hit is free
        bool free_hit = w->uses > 1 && inventory_has(ITEM_CHARM_STAKEOUT) && (++g.stabs % 5) == 1;  // 1 in 5 stabs free: 0.8x wear
        if (!free_hit) wear_out(w);
    }
    out->fired = true;
    return true;
}

bool gear_absorb_hit(void) {
    if (g.grit <= 0) return false;
    g.grit--;
    return true;
}

bool gear_survive_fatal(void) {
    if (g.eye_spy_used || !inventory_has(ITEM_CHARM_EYE_SPY)) return false;
    g.eye_spy_used = 1;
    return true;
}

void gear_refill_grit(void) { g.grit = g.steroids; }

// Which recipes each specimen teaches (indices into crafting.c's table).
bool gear_analyse_specimen(void) {
    static const struct { int item; unsigned mask; } k_teach[] = {
        { ITEM_SPECIMEN_DENATURED, (1u << 2) | (1u << 3) },
        { ITEM_SPECIMEN_CONVERGED, (1u << 4) },
        { ITEM_SPECIMEN_REVERSIBLE, (1u << 5) | (1u << 6) },
    };
    for (int i = 0; i < 3; i++) {
        if (!inventory_has(k_teach[i].item)) continue;
        inventory_remove(k_teach[i].item);
        for (int r = 0; r < 32; r++) {
            if (k_teach[i].mask & (1u << r)) crafting_learn(r);
        }
        return true;
    }
    return false;
}

void gear_export(int* out) {
    int v[GEAR_SAVE_INTS] = {
        g.ammo[0], g.ammo[1], g.ammo[2], g.blood, g.blood_cap, g.scrap, g.rare_metal,
        g.steroids, g.stabilizer, g.grit, g.collectibles, g.equipped, g.active, g.wear, g.eye_spy_used,
    };
    memcpy(out, v, sizeof(v));
}

void gear_import(const int* in) {
    g.ammo[0] = in[0]; g.ammo[1] = in[1]; g.ammo[2] = in[2];
    g.blood = in[3]; g.blood_cap = in[4] > 0 ? in[4] : GEAR_BLOOD_CAP;
    g.scrap = in[5]; g.rare_metal = in[6];
    g.steroids = in[7]; g.stabilizer = in[8]; g.grit = in[9]; g.collectibles = in[10];
    g.equipped = in[11]; g.active = in[12]; g.wear = in[13]; g.eye_spy_used = in[14];
}
