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

enemy_state_t enemy_state(void);
// The sense that found Grace on the last update.
enemy_sense_t enemy_sense(void);
float enemy_x(void);
float enemy_y(void);
