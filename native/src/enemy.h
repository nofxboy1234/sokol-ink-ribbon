#pragma once
#include <stdbool.h>
#include "vec2.h"
#include "player.h"
#include "webexport.h"

#define ENEMY_COUNT 1

typedef enum {
    // wandering a chosen patrol route, re-picking a destination as it goes
    ENEMY_PATROL,
    // close enough to notice Grace, closing on her
    ENEMY_CHASE,
    // next to Grace, swinging at her
    ENEMY_ATTACK,
} enemy_mode_t;

// number of enemies on the map
int enemy_count(void);

void enemy_init(void);

// Advance the enemies by one step of Grace's movement. The map is turn-based:
// they only move while Grace does, so this is called once per cell she covers.
// This is where they decide where to go and whether to attack.
void enemy_turn(void);

// Slide the enemies towards their next cell. Called every frame, but only does
// anything on frames where Grace herself moved, which is what makes the map
// turn-based: an enemy glides while she walks and stands still when she does.
void enemy_update(float dt);

// Number of enemies currently able to act, for the status readout.
int enemy_active(void);

enemy_mode_t enemy_mode(int e);

// position in map units
vec2_t enemy_position(int e);

// Cell coordinates as plain returns, for callers that only need one axis.
WEB_EXPORT int enemy_cell_x(int e);
WEB_EXPORT int enemy_cell_y(int e);

// Continuous position in map units, which moves between cells as the enemy walks.
WEB_EXPORT float enemy_pos_x(int e);
WEB_EXPORT float enemy_pos_y(int e);

// 0 patrol, 1 chase, 2 attack.
WEB_EXPORT int enemy_mode_id(int e);

// 0 or 1 while an enemy has noticed Grace
bool enemy_aggro(void);

// Called from the player's own movement so enemies can react as she moves.
void enemy_notify_player_moved(void);
