#pragma once
#include <stdbool.h>

typedef enum {
    ENEMY_PATROL = 0,
    ENEMY_LOOK,
    ENEMY_CHASE,
    ENEMY_SEARCH,
    ENEMY_STUNNED,
    ENEMY_WINDED,
    ENEMY_BREAKING,
} enemy_state_t;

typedef enum {
    SENSE_NONE = 0,
    SENSE_SIGHT,
    SENSE_HEARING,
    SENSE_SMELL,
} enemy_sense_t;

void enemy_init(int avoid_x, int avoid_y);
// noise is how far Grace's footsteps carry this frame, in cells.
// Returns true on the frame the enemy touches Grace.
bool enemy_update(float dt, float grace_x, float grace_y, float noise);
void enemy_stun(float seconds);
// A bottle hit: stunned and flashing for the given seconds.
void enemy_damage(float seconds);
// Seconds of damage flashing left (0 when not flashing).
float enemy_flash(void);
// A noise at a cell carrying radius cells; returns true if it went to look.
bool enemy_hear_noise(int x, int y, float radius);
// The cell it last sensed Grace at, or the noise it is investigating.
int enemy_focus_x(void);
int enemy_focus_y(void);
// Tests: fix the random stream and put it somewhere.
void enemy_seed(unsigned int seed);
void enemy_place(float x, float y);

enemy_state_t enemy_state(void);
// The sense that found Grace on the last update.
enemy_sense_t enemy_sense(void);
float enemy_x(void);
float enemy_y(void);
