#include "gear.h"
#include "crafting.h"
#include "enemy.h"
#include "health.h"
#include "inventory.h"
#include "level.h"
#include "throw.h"

#include <math.h>
#include <string.h>

// Damage: an enemy has ENEMY_HP (100). The handguns kill in 4 to 6 hits
// (M232 17-25, B934 17-22), a critical hit kills outright; the Requiem
// (Power 85 of 100, "kills most zombies with a single shot") kills in one.
static const weapon_t k_weapons[] = {
    { ITEM_GUN_M232, AMMO_HANDGUN, 10.0f, 17.0f, 25.0f, 0.08f, 12.0f, 9, 9, 1.6f, 0, false },
    { ITEM_GUN_B934, AMMO_HANDGUN, 9.0f, 17.0f, 22.0f, 0.06f, 12.0f, 7, 0, 1.8f, 0, false },
    { ITEM_GUN_REQUIEM, AMMO_MAGNUM, 14.0f, 100.0f, 100.0f, 0.0f, 16.0f, 5, 1, 2.5f, 0, false },
    { ITEM_HUNTING_KNIFE, -1, GEAR_MELEE_REACH, 12.0f, 16.0f, 0.05f, 2.0f, 0, 0, 0.0f, 4, false },
    { ITEM_MAKESHIFT_KNIFE, -1, GEAR_MELEE_REACH, 8.0f, 12.0f, 0.03f, 2.0f, 0, 0, 0.0f, 12, false },
    { ITEM_HEMOLYTIC_INJECTOR, -1, GEAR_MELEE_REACH, 100.0f, 100.0f, 0.0f, 0.0f, 0, 0, 0.0f, 1, true },
};
#define WEAPON_COUNT ((int)(sizeof(k_weapons) / sizeof(k_weapons[0])))

static const float k_stabilizer_mult[GEAR_MAX_DOSES + 1] = { 1.0f, 1.1f, 1.15f, 1.2f, 1.25f };

static struct {
    int ammo[AMMO_KIND_COUNT];
    int blood, blood_cap;
    int scrap, rare_metal;
    int steroids, stabilizer;
    int collectibles;
    int coins;
    int equipped;  // item or -1
    int active;
    int wear;      // uses left on the melee weapon in hand
    int eye_spy_used;
    int stabs;     // knife hits, for the Stakeout Takeout Charm
    int loaded[WEAPON_COUNT];
    float reload_left;  // seconds; > 0 while reloading the equipped gun
    unsigned int rng;
} g;

static int weapon_index(int item) {
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (k_weapons[i].item == item) return i;
    }
    return -1;
}

void gear_init(void) {
    unsigned int rng = g.rng ? g.rng : 0x9e3779b9u;
    memset(&g, 0, sizeof(g));
    g.blood_cap = GEAR_BLOOD_CAP;
    g.equipped = -1;
    g.rng = rng;
}

void gear_seed(unsigned int seed) { g.rng = seed ? seed : 1u; }

static float roll(void) {
    g.rng ^= g.rng << 13;
    g.rng ^= g.rng >> 17;
    g.rng ^= g.rng << 5;
    return (float)(g.rng & 0xffffffu) / (float)0x1000000;
}

const weapon_t* gear_weapon(int item) {
    int i = weapon_index(item);
    return i >= 0 ? &k_weapons[i] : 0;
}

static int ammo_kind_of(int item) {
    switch (item) {
        case ITEM_HANDGUN_AMMO: return AMMO_HANDGUN;
        case ITEM_MAGNUM_AMMO: return AMMO_MAGNUM;
        default: return -1;
    }
}

static int* counter_of(int item) {
    int a = ammo_kind_of(item);
    if (a >= 0) return &g.ammo[a];
    if (item == ITEM_SCRAP) return &g.scrap;
    if (item == ITEM_RARE_METAL) return &g.rare_metal;
    if (item == ITEM_INFECTED_BLOOD) return &g.blood;
    if (item == ITEM_MR_RACCOON) return &g.collectibles;
    if (item == ITEM_COIN) return &g.coins;  // Antique Coins stack in one place (RE)
    return 0;
}

bool gear_counted(int item) { return counter_of(item) != 0; }

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
            if (!inventory_has(ITEM_BLOOD_COLLECTOR) || g.blood >= g.blood_cap) return false;
            return gear_give(item, qty);
        case ICAT_POUCH:
            inventory_expand(2);
            return true;
        case ICAT_GUN: {
            if (!inventory_add(item)) return false;
            int w = weapon_index(item);
            if (w >= 0) g.loaded[w] = k_weapons[w].found_loaded;
            if (g.equipped < 0 && !throw_equipped()) gear_equip(item);
            return true;
        }
        default:
            // the Unicorn Trinket Box opens in her hands: the Red Jewel
            if (item == ITEM_UNICORN_BOX) return inventory_add(ITEM_RED_JEWEL);
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

bool gear_use_slot(int index) {
    int item = inventory_slot(index);
    switch (item) {
        case ITEM_HERB:
        case ITEM_MIXED_HERB:
        case ITEM_INJECTOR:
            if (health_hp() >= health_max()) return false;
            if (!inventory_clear_slot(index)) return false;
            if (item == ITEM_HERB) health_heal(1);
            else health_heal_full();
            return true;
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
        case ITEM_STEROIDS:  // permanent: +1 max health, and a full heal
            if (g.steroids >= GEAR_MAX_DOSES) return false;
            inventory_clear_slot(index);
            g.steroids++;
            health_raise_max(1);
            return true;
        case ITEM_STABILIZER:  // permanent: more damage per bullet
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
        g.reload_left = 0.0f;
    }
    g.equipped = item;
    g.active = 0;
    return true;
}

void gear_unequip(void) {
    g.equipped = -1;
    g.active = 0;
    g.reload_left = 0.0f;
}

bool gear_active(void) { return g.active && g.equipped >= 0; }

bool gear_ready(void) {
    if (g.equipped < 0 || !inventory_has(g.equipped)) return false;
    int i = weapon_index(g.equipped);
    const weapon_t* w = &k_weapons[i];
    if (w->ammo >= 0) return g.loaded[i] > 0 || g.ammo[w->ammo] > 0;
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
    return w ? w->range : THROW_RANGE_CELLS;
}

int gear_loaded(int item) {
    int i = weapon_index(item);
    return i >= 0 && k_weapons[i].mag > 0 ? g.loaded[i] : -1;
}

void gear_set_loaded(int item, int rounds) {
    int i = weapon_index(item);
    if (i < 0 || k_weapons[i].mag <= 0) return;
    g.loaded[i] = rounds < 0 ? 0 : rounds > k_weapons[i].mag ? k_weapons[i].mag : rounds;
}

bool gear_reloading(void) { return g.reload_left > 0.0f; }

bool gear_reload(void) {
    int i = weapon_index(g.equipped);
    if (i < 0 || g.reload_left > 0.0f) return false;
    const weapon_t* w = &k_weapons[i];
    if (w->mag <= 0 || g.loaded[i] >= w->mag || g.ammo[w->ammo] <= 0) return false;
    g.reload_left = w->reload;
    return true;
}

bool gear_update(float dt) {
    if (g.reload_left <= 0.0f) return false;
    g.reload_left -= dt;
    if (g.reload_left > 0.0f) return false;
    g.reload_left = 0.0f;
    int i = weapon_index(g.equipped);
    if (i < 0) return false;
    const weapon_t* w = &k_weapons[i];
    int take = w->mag - g.loaded[i];
    if (take > g.ammo[w->ammo]) take = g.ammo[w->ammo];
    g.loaded[i] += take;
    g.ammo[w->ammo] -= take;
    return true;
}

static float damage_mult(const weapon_t* w) {
    if (w->ammo >= 0) return k_stabilizer_mult[g.stabilizer];
    if (w->uses > 1 && inventory_has(ITEM_CHARM_STAKEOUT)) return 1.5f;
    return 1.0f;
}

static void wear_out(const weapon_t* w) {
    if (--g.wear > 0) return;
    inventory_remove(w->item);
    g.wear = w->uses;
    if (!inventory_has(w->item)) gear_unequip();
}

bool gear_fire(float gx, float gy, float dx, float dy, float len, gear_shot_t* out) {
    (void)len;
    memset(out, 0, sizeof(*out));
    if (!gear_active() || !gear_ready() || g.reload_left > 0.0f) return false;
    int i = weapon_index(g.equipped);
    const weapon_t* w = &k_weapons[i];
    if (w->mag > 0 && g.loaded[i] <= 0) {
        gear_reload();  // an empty magazine: reload instead of firing
        return false;
    }
    float range = gear_range();
    if (w->needs_stun) {
        enemy_state_t s = enemy_state();
        float ex = enemy_x() - gx, ey = enemy_y() - gy;
        if ((s != ENEMY_STUNNED && s != ENEMY_PATROL) || ex * ex + ey * ey > range * range) return false;
    }
    float n = sqrtf(dx * dx + dy * dy);
    if (n < 1e-6f) return false;
    throw_scan_t scan;
    throw_hitscan(gx, gy, dx / n, dy / n, range, &scan);
    out->x = scan.x;
    out->y = scan.y;
    out->hit = scan.hit_enemy && enemy_alive();
    if (out->hit) {
        out->critical = roll() < w->crit;
        out->damage = (w->damage_min + (w->damage_max - w->damage_min) * roll()) * damage_mult(w);
        out->killed = enemy_hurt(out->damage, out->critical);
    }
    if (w->noise > 0.0f) {
        enemy_hear_noise((int)floorf(out->x), (int)floorf(out->y), w->noise);
    }
    if (w->mag > 0) {
        g.loaded[i]--;
        if (g.loaded[i] == 0 && !gear_reload() && g.ammo[w->ammo] == 0) g.active = 0;
    } else if (out->hit) {
        // Stakeout Takeout: 1 in 5 stabs is free (0.8x wear)
        bool free_hit = w->uses > 1 && inventory_has(ITEM_CHARM_STAKEOUT) && (++g.stabs % 5) == 1;
        if (!free_hit) wear_out(w);
    }
    out->fired = true;
    return true;
}

bool gear_survive_fatal(void) {
    if (g.eye_spy_used || !inventory_has(ITEM_CHARM_EYE_SPY)) return false;
    g.eye_spy_used = 1;
    return true;
}

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

_Static_assert(14 + WEAPON_COUNT < GEAR_SAVE_INTS, "gear save size");

void gear_export(int* out) {
    memset(out, 0, sizeof(int) * GEAR_SAVE_INTS);
    int v[14] = {
        g.ammo[0], g.ammo[1], g.blood, g.blood_cap, g.scrap, g.rare_metal, g.steroids,
        g.stabilizer, g.collectibles, g.equipped, g.active, g.wear, g.eye_spy_used, g.stabs,
    };
    memcpy(out, v, sizeof(v));
    for (int i = 0; i < WEAPON_COUNT; i++) out[14 + i] = g.loaded[i];
    out[GEAR_SAVE_INTS - 1] = g.coins;
}

void gear_import(const int* in) {
    g.ammo[0] = in[0]; g.ammo[1] = in[1];
    g.blood = in[2]; g.blood_cap = in[3] > 0 ? in[3] : GEAR_BLOOD_CAP;
    g.scrap = in[4]; g.rare_metal = in[5];
    g.steroids = in[6]; g.stabilizer = in[7]; g.collectibles = in[8];
    g.equipped = in[9]; g.active = in[10]; g.wear = in[11]; g.eye_spy_used = in[12]; g.stabs = in[13];
    for (int i = 0; i < WEAPON_COUNT; i++) g.loaded[i] = in[14 + i];
    g.coins = in[GEAR_SAVE_INTS - 1];
    g.reload_left = 0.0f;
}
