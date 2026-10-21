#pragma once
#include <stdbool.h>

// Grace's gear beyond plain inventory slots: ammo, crafting materials, the
// Blood Collector, upgrades, charms and the weapon in her hand. What each
// item does comes from its category in the item catalog (level.c).

#define GEAR_BLOOD_CAP 100        // the Blood Collector's starting limit
#define GEAR_BLOOD_CAP_STEP 50    // what the Override Manual adds
#define GEAR_TRANSFUSION_UNITS 60 // blood in one Transfusion Bag
#define GEAR_MAX_DOSES 4          // Steroids and Stabilizer doses each
#define GEAR_MELEE_REACH 1.6f     // cells: a knife reaches the next cell

typedef enum {
    AMMO_HANDGUN = 0,
    AMMO_MAGNUM,
    AMMO_SHELLS,
    AMMO_KIND_COUNT,
} ammo_kind_t;

// One weapon's numbers. Guns spend ammo; melee wears out; a throwable is
// spent per throw.
typedef struct {
    int item;
    int ammo;         // ammo_kind_t for guns, -1 otherwise
    float range;      // cells
    float stun;       // seconds the enemy is stunned on a hit
    float noise;      // cells the shot or swing carries
    int pellets;      // rays per shot (shotgun spread)
    int uses;         // melee: hits before it breaks
    float blast;      // throwable: stuns the enemy within this many cells of the impact
    bool needs_stun;  // Hemolytic Injector: only works on a stunned or unaware enemy
} weapon_t;

void gear_init(void);
const weapon_t* gear_weapon(int item);

// Picking up an item: counted kinds go to their counters, the rest to a
// slot. False if she can't take it (inventory full, no Blood Collector).
bool gear_pickup(int item, int qty);
// How many she has of an item: a counter for counted kinds, else slots.
int gear_count(int item);
// True for kinds kept as a number instead of in slots (ammo, blood, scrap).
bool gear_counted(int item);
bool gear_spend(int item, int qty);
bool gear_give(int item, int qty);

int gear_ammo(int kind);
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
// Equipped and usable right now (a gun with ammo, a melee weapon left).
bool gear_ready(void);
float gear_range(void);

typedef struct {
    bool fired;
    bool hit;
    float x, y;  // impact (world cells)
} gear_shot_t;
// Fire, swing or throw the active weapon along a ray from (gx, gy).
bool gear_fire(float gx, float gy, float dx, float dy, float len, gear_shot_t* out);

// A hit on Grace: true if her gear absorbed it (Steroids grit) and her
// health should not drop.
bool gear_absorb_hit(void);
// A hit that would kill her: true if the Eye Spy Charm saves her.
bool gear_survive_fatal(void);
// She healed to full: Steroids grit refills.
void gear_refill_grit(void);
int gear_grit(void);

// Laser Microscope: analyse any carried specimen. True if one was used.
bool gear_analyse_specimen(void);

// Save slots.
#define GEAR_SAVE_INTS 24
void gear_export(int* out);
void gear_import(const int* in);
