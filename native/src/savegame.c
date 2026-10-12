//------------------------------------------------------------------------------
//  savegame.c
//  In-memory save slots capturing the run: time, position, inventory, health,
//  item pickups and the level's object state (doors, lights, openables...).
//------------------------------------------------------------------------------
#include "savegame.h"
#include "doors.h"
#include "grid.h"
#include "health.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "player.h"

#include <string.h>

typedef struct {
    bool used;
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
    return true;
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
    if (elapsed_ms) {
        *elapsed_ms = s->elapsed_ms;
    }
    return true;
}
