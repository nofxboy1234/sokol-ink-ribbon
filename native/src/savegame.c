//------------------------------------------------------------------------------
//  savegame.c
//  In-memory save slots capturing the run: time, position, inventory, health,
//  item pickups and the level's object state (doors, lights, openables...).
//------------------------------------------------------------------------------
#include "savegame.h"
#include "doors.h"
#include "enemy.h"
#include "grid.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "player.h"
#include "throw.h"

#include <string.h>

// 2 added the equipped bottle and the active weapon; older slots load unequipped.
// 3 keeps an equipped slot with no bottle left (greyed out).
// 4 adds the enemy (position, state, timers, anger, route) and the items the
// Lighter has shown; older slots respawn the enemy away from Grace as before.
#define SAVE_VERSION 4

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
    interact_export(&s->lighter, &s->saves, &s->goal, &s->file_count, s->file_obj);
    s->equipped = throw_equipped();
    s->active = throw_active();
    items_export_seen(s->item_seen, LEVEL_MAX_OBJECTS);
    enemy_export(&s->enemy);
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
    interact_restore(s->lighter, s->saves, s->goal, s->file_count, s->file_obj);
    player_set_cell(s->px, s->py);
    doors_restore_from_level();
    bool v2 = s->version >= 2;
    bool keep_empty = s->version >= 3;
    bool equipped = v2 && s->equipped && (keep_empty || inventory_has(ITEM_BOTTLE));
    throw_restore(equipped, equipped && s->active);
    bool v4 = s->version >= 4;
    items_import_seen(v4 ? s->item_seen : NULL, LEVEL_MAX_OBJECTS);
    if (v4) {
        enemy_import(&s->enemy);
    }
    if (elapsed_ms) {
        *elapsed_ms = s->elapsed_ms;
    }
    return true;
}
