//------------------------------------------------------------------------------
//  savegame.c
//  In-memory save slots capturing the run: time, position, inventory, health,
//  item pickups and the level's object state (doors, lights, openables...).
//------------------------------------------------------------------------------
#include "savegame.h"
#include "doors.h"
#include "enemy.h"
#include "grid.h"
#include "gear.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "crafting.h"
#include "items.h"
#include "level.h"
#include "player.h"
#include "throw.h"

#include <string.h>

// 2 added the equipped bottle and the active weapon; older slots load unequipped.
// 3 keeps an equipped slot with no bottle left (greyed out).
// 4 adds the enemy (position, state, timers, anger, route) and the items the
// Lighter has shown; older slots respawn the enemy away from Grace as before.
// 5 adds inventory capacity, known recipes, level-complete, on-top and the
// last save slot is tracked in the app (not the slot); older slots get the
// defaults (8 slots, recipe 0 only, not complete, on the floor).
// 6 adds gear (ammo, blood, materials, doses, the weapon in hand).
#define SAVE_VERSION 6

typedef struct {
    bool used;
    int version;
    double elapsed_ms;
    int px, py;
    int health;
    int inv_count;
    int inv[INV_SLOTS];
    int obj_count;
    unsigned char obj_state[LEVEL_MAX_OBJECTS];
    unsigned char obj_open[LEVEL_MAX_OBJECTS];
    unsigned char item_taken[LEVEL_MAX_OBJECTS];
    bool lighter;
    int saves;
    bool goal;
    int file_count;
    int file_obj[LEVEL_MAX_OBJECTS];
    bool equipped;
    bool active;
    unsigned char item_seen[LEVEL_MAX_OBJECTS];
    enemy_save_t enemy;
    int inv_capacity;
    unsigned int known_recipes;
    bool complete;
    bool on_top;
    int gear[GEAR_SAVE_INTS];
    int key_count;
    int keys[INV_KEY_RING];
} save_slot_t;

static save_slot_t g_slots[SAVE_SLOTS];

void savegame_init(void) {
    memset(g_slots, 0, sizeof(g_slots));
}

bool savegame_used(int slot) {
    return slot >= 0 && slot < SAVE_SLOTS && g_slots[slot].used;
}

double savegame_time(int slot) {
    return (slot >= 0 && slot < SAVE_SLOTS) ? g_slots[slot].elapsed_ms : 0.0;
}

bool savegame_save(int slot, double elapsed_ms) {
    if (slot < 0 || slot >= SAVE_SLOTS) {
        return false;
    }
    save_slot_t* s = &g_slots[slot];
    const level_t* lv = grid_level();
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->version = SAVE_VERSION;
    s->elapsed_ms = elapsed_ms;
    player_cell(&s->px, &s->py);
    s->health = health_state();
    // Store every cell, including holes left by Move.
    s->inv_count = INV_SLOTS;
    for (int i = 0; i < INV_SLOTS; i++) {
        s->inv[i] = inventory_slot(i);
    }
    s->obj_count = lv->obj_count;
    for (int i = 0; i < lv->obj_count && i < LEVEL_MAX_OBJECTS; i++) {
        s->obj_state[i] = (unsigned char)lv->objs[i].state;
        s->obj_open[i] = (unsigned char)lv->objs[i].open;
    }
    items_export(s->item_taken, LEVEL_MAX_OBJECTS);
    interact_export(&s->lighter, &s->saves, &s->goal, &s->file_count, s->file_obj, &s->complete);
    s->equipped = throw_equipped();
    s->active = throw_active();
    items_export_seen(s->item_seen, LEVEL_MAX_OBJECTS);
    enemy_export(&s->enemy);
    s->inv_capacity = inventory_capacity();
    s->known_recipes = crafting_known_mask();
    s->on_top = player_on_top();
    gear_export(s->gear);
    s->key_count = inventory_key_count();
    for (int i = 0; i < s->key_count; i++) s->keys[i] = inventory_key(i);
    return true;
}

bool savegame_has_enemy(int slot) {
    return savegame_used(slot) && g_slots[slot].version >= 4;
}

bool savegame_load(int slot, double* elapsed_ms) {
    if (slot < 0 || slot >= SAVE_SLOTS || !g_slots[slot].used) {
        return false;
    }
    save_slot_t* s = &g_slots[slot];
    level_t* lv = (level_t*)grid_level();
    int n = s->obj_count < lv->obj_count ? s->obj_count : lv->obj_count;
    for (int i = 0; i < n; i++) {
        lv->objs[i].state = s->obj_state[i];
        lv->objs[i].open = s->obj_open[i];
    }
    items_import(s->item_taken, LEVEL_MAX_OBJECTS);
    inventory_set(s->inv_count, s->inv);
    health_set(s->health);
    interact_restore(s->lighter, s->saves, s->goal, s->file_count, s->file_obj, s->version >= 5 && s->complete);
    player_set_cell(s->px, s->py);
    doors_restore_from_level();
    bool v2 = s->version >= 2;
    bool keep_empty = s->version >= 3;
    bool equipped = v2 && s->equipped && (keep_empty || inventory_has(ITEM_BOTTLE));
    throw_restore(equipped, equipped && s->active);
    if (s->version >= 6) {
        gear_import(s->gear);
        inventory_set_keys(s->key_count, s->keys);
    } else {
        gear_init();
        inventory_set_keys(0, 0);  // older slots kept key items in slots
    }
    bool v4 = s->version >= 4;
    items_import_seen(v4 ? s->item_seen : NULL, LEVEL_MAX_OBJECTS);
    inventory_set_capacity(s->version >= 5 ? s->inv_capacity : INV_BASE_SLOTS);
    crafting_set_known_mask(s->version >= 5 ? s->known_recipes : 1u);
    if (v4) {
        enemy_import(&s->enemy);
    }
    if (elapsed_ms) {
        *elapsed_ms = s->elapsed_ms;
    }
    return true;
}
