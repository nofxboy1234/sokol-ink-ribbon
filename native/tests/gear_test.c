// Native checks for gear, locks and the key ring: magazines, reloads,
// shots to kill, crits and fading bodies, melee wear, blood and the
// collector, Steroids, the Stabilizer and Eye Spy, the Item Box, the Parlor
// cabinets, every lock kind, the wristband ladder, specimens, counter
// crafting and saves. Objects are found by kind, lock and position.
#include "crafting.h"
#include "doors.h"
#include "enemy.h"
#include "gear.h"
#include "grid.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "player.h"
#include "savegame.h"
#include "throw.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static level_t level;
static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        bool ok_ = (cond);                                  \
        printf(ok_ ? "PASS " : "FAIL ");                    \
        if (!ok_) failures++;                               \
        printf(__VA_ARGS__);                                \
        printf("\n");                                       \
    } while (0)

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void reset(void) {
    inventory_init();
    gear_init();
    throw_init();
    health_init();
    crafting_set_known_mask(0);
}

// The door at (x, y), found by position.
static int door_at(int x, int y) {
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (o->kind == OBJ_DOOR && o->x == x && o->y == y) return i;
    }
    return -1;
}

// The first locked door matching a lock kind (and, if > 0, its key).
static int door_with(int lock, int key, bool needs_key2) {
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (o->kind != OBJ_DOOR || o->state != DOOR_LOCKED || o->lock != lock) continue;
        if (key > 0 && o->key_id != key) continue;
        if (needs_key2 && o->key2_id <= 0) continue;
        return i;
    }
    return -1;
}

// Enemy a few cells east of Grace in open floor, for shots along +x.
static bool line_up(int* gx, int* gy, int dist) {
    for (int y = 1; y < level.rows - 1; y++) {
        for (int x = 1; x < level.cols - dist - 1; x++) {
            bool clear = true;
            for (int k = 1; k <= dist && clear; k++) {
                clear = grid_is_floor(x, y) && grid_is_floor(x + k, y) && !grid_blocked(x + k - 1, y, x + k, y) &&
                        !doors_on_edge(x + k - 1, y, x + k, y) &&
                        throw_in_reach(x, y, x + k, y);
            }
            if (clear) {
                *gx = x;
                *gy = y;
                return true;
            }
        }
    }
    return false;
}

static void enemy_at(int x, int y) {
    enemy_init(level.start_x, level.start_y);
    enemy_seed(99u);
    enemy_place((float)x + 0.5f, (float)y + 0.5f);
}

static bool shoot(int gx, int gy, gear_shot_t* shot) {
    return gear_fire((float)gx + 0.5f, (float)gy + 0.5f, 1.0f, 0.0f, 99.0f, shot);
}

int main(int argc, char** argv) {
    char* json = slurp(argc > 1 ? argv[1] : "src/level_01.json");
    if (!json || !level_from_json(&level, json)) {
        printf("could not load the level\n");
        return 2;
    }
    grid_init(&level);
    grid_reveal_rect(0, 0, level.cols, level.rows);
    player_init(level.start_x, level.start_y);
    items_init();
    doors_init();
    interact_init();
    savegame_init();
    throw_seed(7u);

    // --- guns: magazines, reloads, 4-6 shots to kill, crits, death fades ---
    reset();
    gear_seed(1u);
    CHECK(gear_pickup(ITEM_HANDGUN_AMMO, 20) && gear_ammo(AMMO_HANDGUN) == 20, "handgun ammo is a counter, not a slot");
    CHECK(inventory_count() == 0, "ammo takes no slot");
    CHECK(!gear_equip(ITEM_GUN_M232), "no M232 in the bag: can't equip it");
    CHECK(gear_pickup(ITEM_GUN_M232, 1) && gear_equipped() == ITEM_GUN_M232, "picking up the M232 equips it");
    CHECK(gear_loaded(ITEM_GUN_M232) == 9, "the M232 is found with a full 9-round magazine");
    CHECK(gear_set_active(true) && gear_active(), "M232 goes active");
    int gx = 0, gy = 0;
    CHECK(line_up(&gx, &gy, 4), "found an open firing line at %d,%d", gx, gy);
    gear_shot_t shot;
    int min_shots = 99, max_shots = 0, crits = 0;
    for (int trial = 0; trial < 40; trial++) {
        gear_set_loaded(ITEM_GUN_M232, 9);
        gear_give(ITEM_HANDGUN_AMMO, 9);
        enemy_at(gx + 4, gy);
        int shots = 0;
        bool crit = false;
        while (enemy_alive() && shots < 12) {
            if (!shoot(gx, gy, &shot)) break;
            shots++;
            crit = crit || shot.critical;
            if (!shot.killed) CHECK(shot.hit && enemy_state() == ENEMY_STUNNED, "a non-fatal hit staggers (trial %d)", trial);
        }
        if (crit) { crits++; continue; }
        if (shots < min_shots) min_shots = shots;
        if (shots > max_shots) max_shots = shots;
    }
    CHECK(min_shots >= 4 && max_shots <= 6, "M232: 4-6 shots to kill without a crit (%d-%d)", min_shots, max_shots);
    CHECK(crits > 0, "a critical hit sometimes kills early (%d of 40)", crits);
    {   // a critical hit kills outright
        enemy_at(gx + 4, gy);
        CHECK(enemy_hurt(1.0f, true) && !enemy_alive() && enemy_state() == ENEMY_DEAD, "a critical hit kills in one");
        CHECK(!enemy_hurt(50.0f, false), "the dead take no more damage");
        enemy_update(1.0f, (float)gx, (float)gy, 0.0f);
        float half = enemy_alpha();
        enemy_update(2.0f, (float)gx, (float)gy, 0.0f);
        CHECK(half > 0.0f && half < 1.0f && enemy_alpha() == 0.0f, "the body fades away (%.2f -> %.2f)", half, enemy_alpha());
        gear_set_loaded(ITEM_GUN_M232, 9);
        CHECK(shoot(gx, gy, &shot) && !shot.hit, "shots pass over the dead");
    }
    // magazine and reload
    reset();
    gear_pickup(ITEM_GUN_M232, 1);
    gear_set_active(true);
    gear_pickup(ITEM_HANDGUN_AMMO, 5);
    enemy_at(gx + 4, gy);
    for (int k = 0; k < 9; k++) shoot(gx, gy, &shot);
    CHECK(gear_loaded(ITEM_GUN_M232) == 0 && gear_ammo(AMMO_HANDGUN) == 5, "9 shots empty the magazine; the reserve is untouched");
    CHECK(gear_reloading(), "an empty magazine starts a reload");
    CHECK(!shoot(gx, gy, &shot), "no shot while reloading");
    CHECK(!gear_update(1.0f) && gear_update(1.0f), "the M232 reload takes 1.6 s");
    CHECK(gear_loaded(ITEM_GUN_M232) == 5 && gear_ammo(AMMO_HANDGUN) == 0, "it loads what the reserve has (5)");
    CHECK(!gear_reload(), "no reserve: nothing to reload");
    gear_pickup(ITEM_HANDGUN_AMMO, 10);
    CHECK(gear_reload() && gear_update(2.0f) && gear_loaded(ITEM_GUN_M232) == 9 && gear_ammo(AMMO_HANDGUN) == 6,
          "a manual reload tops the magazine up to 9");
    CHECK(gear_pickup(ITEM_GUN_B934, 1) && gear_loaded(ITEM_GUN_B934) == 0, "the B934 is found empty (Prima)");
    // the Stabilizer raises bullet damage
    gear_equip(ITEM_GUN_M232);
    gear_set_active(true);
    float plain = 0.0f, boosted = 0.0f;
    for (int k = 0; k < 20; k++) {
        gear_set_loaded(ITEM_GUN_M232, 9);
        enemy_at(gx + 4, gy);
        if (shoot(gx, gy, &shot) && !shot.critical) plain += shot.damage;
    }
    gear_pickup(ITEM_STABILIZER, 1);
    int bag = -1;
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_STABILIZER) bag = i;
    CHECK(gear_use_slot(bag) && gear_doses_stabilizer() == 1, "a Stabilizer dose is taken");
    for (int k = 0; k < 20; k++) {
        gear_set_loaded(ITEM_GUN_M232, 9);
        enemy_at(gx + 4, gy);
        if (shoot(gx, gy, &shot) && !shot.critical) boosted += shot.damage;
    }
    CHECK(boosted > plain, "the Stabilizer raises bullet damage (%.0f -> %.0f over 20 shots)", plain, boosted);
    // the Requiem: one 12.7mm round kills
    reset();
    CHECK(gear_pickup(ITEM_GUN_REQUIEM, 1) && gear_equip(ITEM_GUN_REQUIEM) && gear_loaded(ITEM_GUN_REQUIEM) == 1,
          "the Requiem comes with one round loaded");
    CHECK(gear_set_active(true), "one round arms it");
    enemy_at(gx + 4, gy);
    CHECK(shoot(gx, gy, &shot) && shot.killed, "one Requiem round kills");
    CHECK(gear_loaded(ITEM_GUN_REQUIEM) == 0 && !gear_active(), "empty with no reserve: she lowers it");
    CHECK(gear_ammo(AMMO_HANDGUN) == 0, "the Requiem never spends handgun ammo");
    CHECK(!throw_equipped(), "a gun takes the bottle's East slot");

    // --- knife wear and the Stakeout Takeout Charm ---
    reset();
    gear_pickup(ITEM_HUNTING_KNIFE, 1);
    gear_equip(ITEM_HUNTING_KNIFE);
    gear_set_active(true);
    int hits = 0;
    for (int i = 0; i < 10 && inventory_has(ITEM_HUNTING_KNIFE); i++) {
        enemy_at(gx + 1, gy);
        if (shoot(gx, gy, &shot) && shot.hit) hits++;
    }
    CHECK(hits == 4 && !inventory_has(ITEM_HUNTING_KNIFE) && gear_equipped() < 0, "hunting knife breaks after 4 hits (%d)", hits);
    reset();
    gear_pickup(ITEM_CHARM_STAKEOUT, 1);
    gear_pickup(ITEM_HUNTING_KNIFE, 1);
    gear_equip(ITEM_HUNTING_KNIFE);
    gear_set_active(true);
    hits = 0;
    for (int i = 0; i < 10 && inventory_has(ITEM_HUNTING_KNIFE); i++) {
        enemy_at(gx + 1, gy);
        if (shoot(gx, gy, &shot) && shot.hit) hits++;
    }
    CHECK(hits == 5, "Stakeout charm: every fifth stab is free, so 5 hits (%d)", hits);

    // --- blood, the collector, transfusion bags and the override manual ---
    reset();
    CHECK(!gear_pickup(ITEM_INFECTED_BLOOD, 20), "no Blood Collector: blood stays on the floor");
    gear_pickup(ITEM_BLOOD_COLLECTOR, 1);
    CHECK(gear_pickup(ITEM_INFECTED_BLOOD, 90) && gear_blood() == 90, "collector holds blood");
    gear_pickup(ITEM_TRANSFUSION_BAG, 1);
    bag = -1;
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_TRANSFUSION_BAG) bag = i;
    bool used = gear_use_slot(bag);
    CHECK(used && gear_blood() == 100, "a transfusion bag tops up to the 100 cap (slot %d used %d blood %d)", bag, used, gear_blood());
    gear_pickup(ITEM_OVERRIDE_MANUAL, 1);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_OVERRIDE_MANUAL) bag = i;
    CHECK(gear_use_slot(bag) && gear_blood_cap() == 150, "override manual lifts the cap by 50");

    // --- Steroids raise max health; the Eye Spy Charm ---
    reset();
    CHECK(health_hp() == HEALTH_BASE_MAX && health_max() == HEALTH_BASE_MAX, "she starts at %d of %d", health_hp(), health_max());
    health_hurt();
    health_hurt();
    gear_pickup(ITEM_STEROIDS, 1);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_STEROIDS) bag = i;
    CHECK(gear_use_slot(bag) && health_max() == HEALTH_BASE_MAX + 1 && health_hp() == health_max(),
          "Steroids: +1 max health and a full heal (%d of %d)", health_hp(), health_max());
    CHECK(!health_hurt() && !health_hurt() && !health_hurt() && health_hurt(), "with 4 max she survives 3 hits, the 4th is fatal");
    health_heal(1);
    gear_pickup(ITEM_HERB, 1);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_HERB) bag = i;
    CHECK(gear_use_slot(bag) && health_hp() == 2, "a Green Herb heals one point");
    CHECK(!gear_survive_fatal(), "no Eye Spy: a fatal hit is fatal");
    gear_pickup(ITEM_CHARM_EYE_SPY, 1);
    CHECK(gear_survive_fatal() && !gear_survive_fatal(), "Eye Spy saves her once");

    // --- the Item Box ---
    reset();
    gear_pickup(ITEM_GUN_M232, 1);
    inventory_add(ITEM_HERB);
    int herb = -1;
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_HERB) herb = i;
    CHECK(inventory_box_store(herb) && inventory_box_count() == 1 && !inventory_has(ITEM_HERB), "a herb goes into the Item Box");
    CHECK(inventory_box_take(0) && inventory_has(ITEM_HERB) && inventory_box_count() == 0, "and comes back out");
    {
        int box = -1;
        for (int i = 0; i < level.obj_count; i++) if (level.objs[i].kind == OBJ_ITEMBOX) { box = i; break; }
        CHECK(box >= 0, "the level has an Item Box");
        if (box >= 0) {
            const obj_t* o = &level.objs[box];
            static const int nx[4] = { 0, 0, 1, -1 }, ny[4] = { 1, -1, 0, 0 };
            bool opened = false;
            for (int k = 0; k < 4 && !opened; k++) {
                if (!grid_is_floor(o->x + nx[k], o->y + ny[k])) continue;
                player_init(o->x + nx[k], o->y + ny[k]);
                interact_at(o->x, o->y);
                opened = interact_take_itembox();
            }
            CHECK(opened, "using the Item Box at %d,%d opens it", o->x, o->y);
            player_init(level.start_x, level.start_y);
        }
    }

    // --- the Parlor: coin cabinets sell the injectors ---
    reset();
    {
        int cab = -1;
        for (int i = 0; i < level.obj_count; i++) {
            const obj_t* o = &level.objs[i];
            if (o->kind == OBJ_CABINET && o->item_type == ITEM_STEROIDS && o->qty > 0) cab = i;
        }
        CHECK(cab >= 0 && level.objs[cab].code == 4, "the Parlor sells Steroids for 4 coins");
        if (cab >= 0) {
            obj_t* o = &level.objs[cab];
            static const int nx[4] = { 0, 0, 1, -1 }, ny[4] = { 1, -1, 0, 0 };
            int fx = -1, fy = -1;
            for (int k = 0; k < 4 && fx < 0; k++) if (grid_is_floor(o->x + nx[k], o->y + ny[k])) { fx = o->x + nx[k]; fy = o->y + ny[k]; }
            player_init(fx, fy);
            gear_pickup(ITEM_COIN, 3);
            interact_at(o->x, o->y);
            CHECK(!o->state && !inventory_has(ITEM_STEROIDS) && gear_count(ITEM_COIN) == 3, "3 coins don't buy it");
            gear_pickup(ITEM_COIN, 2);
            interact_at(o->x, o->y);
            CHECK(o->state && inventory_has(ITEM_STEROIDS) && gear_count(ITEM_COIN) == 1, "5 coins buy it, 1 left");
            CHECK(inventory_count() == 1, "Antique Coins stack: they take no slot");
            o->state = 0;
            player_init(level.start_x, level.start_y);
        }
    }

    // --- locks ---
    reset();
    int d = door_with(LOCK_KEY, 0, true);
    CHECK(d >= 0, "a two-key door exists (%s at %d,%d)", d >= 0 ? level.objs[d].name : "-", d >= 0 ? level.objs[d].x : 0, d >= 0 ? level.objs[d].y : 0);
    if (d >= 0) {
        obj_t* o = &level.objs[d];
        inventory_add(o->key_id);
        CHECK(!doors_unlockable(o, o->x, o->y), "one of its two keys is not enough");
        inventory_add(o->key2_id);
        if (o->key3_id > 0) inventory_add(o->key3_id);
        CHECK(doors_unlockable(o, o->x, o->y), "with every key it unlocks");
    }
    reset();
    {  // the F2 Locked Drawer at 63,97 takes a lockpick, and spends it
        int drawer = -1;
        for (int i = 0; i < level.obj_count; i++) {
            const obj_t* o = &level.objs[i];
            if (o->kind == OBJ_OBSTACLE && o->openable && o->x == 63 && o->y == 97) drawer = i;
        }
        CHECK(drawer >= 0 && level.objs[drawer].key_id == ITEM_LOCKPICK, "the Locked Drawer at 63,97 takes a lockpick");
        if (drawer >= 0) {
            static const int nx[4] = { 0, 0, 1, -1 }, ny[4] = { 1, -1, 0, 0 };
            bool opened = false;
            CHECK(!interact_at(63, 97) || !level.objs[drawer].open, "no lockpick: the drawer stays shut");
            gear_pickup(ITEM_LOCKPICK, 1);
            for (int k = 0; k < 4 && !opened; k++) {
                if (!grid_is_floor(63 + nx[k], 97 + ny[k])) continue;
                player_init(63 + nx[k], 97 + ny[k]);
                interact_at(63, 97);
                opened = level.objs[drawer].open != 0;
            }
            CHECK(opened && !inventory_has(ITEM_LOCKPICK), "the lockpick opens it and is spent");
            player_init(level.start_x, level.start_y);
        }
    }
    d = door_with(LOCK_OTHER_SIDE, 0, false);
    CHECK(d >= 0, "a one-sided door exists");
    if (d >= 0) {
        obj_t* o = &level.objs[d];
        int before = o->side > 0 ? -1 : 0, after = o->side > 0 ? 0 : -1;
        int bx = o->horizontal ? o->x : o->x + before, by = o->horizontal ? o->y + before : o->y;
        int ax = o->horizontal ? o->x : o->x + after, ay = o->horizontal ? o->y + after : o->y;
        CHECK(!doors_unlockable(o, bx, by) && doors_unlockable(o, ax, ay), "it opens from one side only (%d,%d)", o->x, o->y);
    }
    d = door_at(148, 94);
    CHECK(d >= 0 && level.objs[d].lock == LOCK_JAMMED, "the F2 Blocked Door at 148,94 is jammed");
    if (d >= 0) {
        for (int i = 0; i < ITEM_COUNT; i++) gear_give(i, 1);
        CHECK(!doors_unlockable(&level.objs[d], 148, 94), "no item opens a jammed door");
    }

    // --- the wristband ladder ---
    reset();
    inventory_add(ITEM_WRISTBAND_3);
    CHECK(doors_has_key(ITEM_WRISTBAND_1) && doors_has_key(ITEM_WRISTBAND_2), "a level 3 wristband opens level 1 and 2 points");
    reset();
    inventory_add(ITEM_WRISTBAND_1);
    CHECK(!doors_has_key(ITEM_WRISTBAND_2), "level 1 does not open level 2");

    // --- specimens and the microscope, counter crafting ---
    reset();
    inventory_add(ITEM_SPECIMEN_DENATURED);
    CHECK(gear_analyse_specimen() && !inventory_has(ITEM_SPECIMEN_DENATURED) && crafting_known(2), "analysing a specimen teaches its recipe");
    CHECK(!gear_analyse_specimen(), "nothing left to analyse");
    int recipe = -1;
    for (int r = 0; r < crafting_count(); r++) {
        for (int p = 0; p < 3; p++) if (crafting_part_item(r, p) == ITEM_INFECTED_BLOOD && recipe < 0 && crafting_known(r)) recipe = r;
    }
    CHECK(recipe >= 0, "a known recipe takes blood (%d)", recipe);
    if (recipe >= 0) {
        CHECK(!crafting_ready(recipe), "without blood it isn't ready");
        gear_pickup(ITEM_BLOOD_COLLECTOR, 1);
        for (int p = 0; p < 3; p++) {
            int it = crafting_part_item(recipe, p);
            if (it >= 0) gear_give(it, crafting_part_qty(recipe, p));
        }
        int blood = gear_blood();
        CHECK(crafting_ready(recipe) && craft(recipe) && gear_blood() < blood, "crafting spends blood from the counter (%d -> %d)", blood, gear_blood());
    }

    // --- key ring and gear survive a save ---
    reset();
    gear_pickup(ITEM_WRENCH, 1);
    gear_pickup(ITEM_EAST_WING_KEYCARD, 1);
    gear_pickup(ITEM_HANDGUN_AMMO, 7);
    gear_pickup(ITEM_GUN_M232, 1);
    CHECK(inventory_key_count() == 2 && inventory_count() == 1, "wrench and keycard go on the key ring, the gun in a slot");
    inventory_add(ITEM_INK_RIBBON);
    inventory_add(ITEM_HERB);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_HERB) bag = i;
    inventory_box_store(bag);
    gear_pickup(ITEM_COIN, 3);
    gear_set_loaded(ITEM_GUN_M232, 4);
    health_raise_max(1);
    health_hurt();
    CHECK(savegame_save(0, 1.0), "saved");
    reset();
    double ms = 0;
    CHECK(savegame_load(0, &ms) && inventory_key_count() == 2 && gear_ammo(AMMO_HANDGUN) == 7 && gear_equipped() == ITEM_GUN_M232,
          "load restores the key ring, ammo and the equipped gun");
    CHECK(gear_loaded(ITEM_GUN_M232) == 4 && gear_count(ITEM_COIN) == 3, "and the rounds loaded and the coins");
    CHECK(health_max() == HEALTH_BASE_MAX + 1 && health_hp() == HEALTH_BASE_MAX, "and health %d of %d", health_hp(), health_max());
    CHECK(inventory_box_count() == 1 && inventory_box_item(0) == ITEM_HERB, "and the Item Box");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
