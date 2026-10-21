// Native checks for gear, locks and the key ring: guns need their ammo,
// melee wears out, blood needs the collector, Steroids and Eye Spy, every
// lock kind, the wristband ladder, specimens, counter crafting and saves.
// Objects are found by kind, lock and position, never by list index.
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

    // --- guns need their own ammo, and a hit stuns ---
    reset();
    CHECK(gear_pickup(ITEM_HANDGUN_AMMO, 6) && gear_ammo(AMMO_HANDGUN) == 6, "handgun ammo is a counter, not a slot");
    CHECK(inventory_count() == 0, "ammo takes no slot");
    CHECK(!gear_equip(ITEM_GUN_M232), "no M232 in the bag: can't equip it");
    CHECK(gear_pickup(ITEM_GUN_M232, 1) && gear_equipped() == ITEM_GUN_M232, "picking up the M232 equips it");
    CHECK(gear_set_active(true) && gear_active(), "M232 with handgun ammo goes active");
    int gx = 0, gy = 0;
    CHECK(line_up(&gx, &gy, 4), "found an open firing line at %d,%d", gx, gy);
    enemy_at(gx + 4, gy);
    gear_shot_t shot;
    CHECK(shoot(gx, gy, &shot) && shot.hit, "M232 shot hits the enemy 4 cells away");
    CHECK(enemy_state() == ENEMY_STUNNED, "the hit stuns her pursuer");
    CHECK(gear_ammo(AMMO_HANDGUN) == 5, "one round spent");
    CHECK(gear_pickup(ITEM_GUN_REQUIEM, 1) && gear_equip(ITEM_GUN_REQUIEM), "Requiem equips");
    CHECK(!gear_set_active(true), "Requiem won't go active on handgun ammo alone");
    gear_pickup(ITEM_MAGNUM_AMMO, 1);
    CHECK(gear_set_active(true), "one 12.7mm round arms the Requiem");
    enemy_at(gx + 4, gy);
    CHECK(shoot(gx, gy, &shot) && gear_ammo(AMMO_MAGNUM) == 0 && !gear_active(), "the last round fires, then she lowers it");
    CHECK(!shoot(gx, gy, &shot), "an empty Requiem can't fire");
    CHECK(gear_ammo(AMMO_HANDGUN) == 5, "Requiem never spends handgun ammo");
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
    int bag = -1;
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_TRANSFUSION_BAG) bag = i;
    bool used = gear_use_slot(bag);
    CHECK(used && gear_blood() == 100, "a transfusion bag tops up to the 100 cap (slot %d used %d blood %d)", bag, used, gear_blood());
    gear_pickup(ITEM_OVERRIDE_MANUAL, 1);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_OVERRIDE_MANUAL) bag = i;
    CHECK(gear_use_slot(bag) && gear_blood_cap() == 150, "override manual lifts the cap by 50");

    // --- Steroids grit and the Eye Spy Charm ---
    reset();
    health_set(2);
    gear_pickup(ITEM_STEROIDS, 1);
    for (int i = 0; i < inventory_capacity(); i++) if (inventory_slot(i) == ITEM_STEROIDS) bag = i;
    CHECK(gear_use_slot(bag) && health_state() == 0 && gear_doses_steroids() == 1, "steroids heal fully and add a dose");
    CHECK(gear_absorb_hit() && !gear_absorb_hit(), "one dose absorbs one hit");
    CHECK(!gear_survive_fatal(), "no Eye Spy: a fatal hit is fatal");
    gear_pickup(ITEM_CHARM_EYE_SPY, 1);
    CHECK(gear_survive_fatal() && !gear_survive_fatal(), "Eye Spy saves her once");

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
    CHECK(savegame_save(0, 1.0), "saved");
    reset();
    double ms = 0;
    CHECK(savegame_load(0, &ms) && inventory_key_count() == 2 && gear_ammo(AMMO_HANDGUN) == 7 && gear_equipped() == ITEM_GUN_M232,
          "load restores the key ring, ammo and the equipped gun");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
