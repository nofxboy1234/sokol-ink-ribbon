#pragma once
#include <stdbool.h>

// The Empty Bottle: equip it from the Inventory, make it the active weapon
// from Quick Equip, then throw it at a cell. Tunables:
#define THROW_RANGE_CELLS 8.0f    // furthest target, centre to centre
#define THROW_HIT_WAIT_PCT 80     // hit chance when she throws standing still
#define THROW_HIT_WALK_PCT 45     // hit chance when she throws on the move
#define THROW_ENEMY_REACH 1       // a target this many cells from the enemy is aimed at it
#define THROW_NOISE_CELLS 10.0f   // how far the smash carries
#define THROW_STUN_SECONDS 3.0f   // a hit stuns (and flashes) the enemy this long

typedef enum {
    THROW_STANCE_WAIT = 0,  // standing or waiting
    THROW_STANCE_WALK,
    THROW_STANCE_RUN,       // no throwing while running
} throw_stance_t;

typedef struct {
    bool aimed;  // the target was on or next to the enemy
    bool hit;
    bool heard;  // a miss or open-floor throw that sent the enemy to look
} throw_result_t;

void throw_init(void);
void throw_seed(unsigned int seed);

bool throw_equipped(void);
bool throw_active(void);
// Equipping needs a bottle in the inventory; activating needs it equipped.
bool throw_set_equipped(bool on);
bool throw_set_active(bool on);
// Drops the equip once the last bottle is gone.
void throw_sync(void);

bool throw_allowed(throw_stance_t stance);
int throw_hit_chance(throw_stance_t stance);
// In range, on the floor, not her own cell, with a clear line.
bool throw_in_reach(int px, int py, int tx, int ty);
bool throw_aims_at_enemy(int tx, int ty);
// Spends a bottle and resolves the throw. False (and nothing spent) if the
// throw isn't allowed.
bool throw_resolve(int px, int py, int tx, int ty, throw_stance_t stance, throw_result_t* out);
