#pragma once
#include <stdbool.h>

// Grace's gear beyond plain inventory slots: ammo, crafting materials, the
// Blood Collector, upgrades, charms and the weapon in her hand. What each
// item does comes from its category in the item catalog (level.c).
// Research (Gamer Guides, GameSpot, Prima): Grace's own weapons are the
// B934 (7 rounds), the S&S M232 (9) and Leon's Requiem (5, 12.7x55mm),
// plus breakable knives and the Hemolytic Injector.

#define GEAR_BLOOD_CAP 100        // the Blood Collector's starting limit
#define GEAR_BLOOD_CAP_STEP 50    // what the Override Manual adds
#define GEAR_TRANSFUSION_UNITS 60 // blood in one Transfusion Bag
#define GEAR_MAX_DOSES 4          // Steroids and Stabilizer doses each
#define GEAR_MELEE_REACH 1.6f     // cells: a knife reaches the next cell

typedef enum {
    AMMO_HANDGUN = 0,
    AMMO_MAGNUM,
    AMMO_KIND_COUNT,
} ammo_kind_t;

// One weapon's numbers. Guns fire from a magazine and reload from the
// reserve; melee wears out. Damage is in enemy hit points (ENEMY_HP 100).
typedef struct {
    int item;
    int ammo;            // ammo_kind_t for guns, -1 otherwise
    float range;         // cells
    float damage_min;    // a hit does a random amount in [min, max]
    float damage_max;
    float crit;          // chance a hit is critical (kills outright)
    float noise;         // cells the shot or swing carries
    int mag;             // rounds a full magazine holds (guns)
    int found_loaded;    // rounds in it when picked up
    float reload;        // seconds to reload
    int uses;            // melee: hits before it breaks
    bool needs_stun;     // Hemolytic Injector: only on a stunned or unaware enemy
} weapon_t;

void gear_init(void);
void gear_seed(unsigned int seed);
const weapon_t* gear_weapon(int item);

// Picking up an item: counted kinds go to their counters, the rest to a
// slot. False if she can't take it (inventory full, no Blood Collector).
bool gear_pickup(int item, int qty);
int gear_count(int item);
bool gear_counted(int item);
bool gear_spend(int item, int qty);
bool gear_give(int item, int qty);

int gear_ammo(int kind);  // the reserve, outside any magazine
int gear_blood(void);
int gear_blood_cap(void);
int gear_doses_steroids(void);
int gear_doses_stabilizer(void);
int gear_collectibles(void);

// Use from the inventory: heal, refill the collector, take a dose.
bool gear_use_slot(int index);

// The weapon in Quick Equip's East slot when it is not the bottle.
int gear_equipped(void);  // item type or -1
bool gear_equip(int item);
void gear_unequip(void);
bool gear_active(void);
bool gear_set_active(bool on);
// Equipped and usable: a gun with rounds loaded or in reserve, a knife left.
bool gear_ready(void);
float gear_range(void);

// Magazines. Rounds loaded in a gun (-1 if not a gun).
int gear_loaded(int item);
void gear_set_loaded(int item, int rounds);
// Start reloading the equipped gun from the reserve; false if it is full,
// there is no reserve, or it is already reloading.
bool gear_reload(void);
bool gear_reloading(void);
// Advance a reload; true when one finished this frame.
bool gear_update(float dt);

typedef struct {
    bool fired;
    bool hit;
    bool critical;
    bool killed;
    float damage;
    float x, y;  // impact (world cells)
} gear_shot_t;
// Fire or swing the active weapon along a ray from (gx, gy). An empty
// magazine starts a reload instead (fired stays false).
bool gear_fire(float gx, float gy, float dx, float dy, float len, gear_shot_t* out);

// A hit that would kill her: true if the Eye Spy Charm saves her (once).
bool gear_survive_fatal(void);

// Laser Microscope: analyse any carried specimen. True if one was used.
bool gear_analyse_specimen(void);

// Save slots.
#define GEAR_SAVE_INTS 24
void gear_export(int* out);
void gear_import(const int* in);
