#pragma once
#include <stdbool.h>

typedef enum {
    ENEMY_PATROL = 0,
    ENEMY_LOOK,
    ENEMY_CHASE,
    ENEMY_SEARCH,
    ENEMY_STUNNED,
    ENEMY_WINDED,
} enemy_state_t;

void enemy_init(int avoid_x, int avoid_y);
// Returns true on the frame the enemy touches Grace.
bool enemy_update(float dt, float grace_x, float grace_y);
void enemy_stun(float seconds);

enemy_state_t enemy_state(void);
float enemy_x(void);
float enemy_y(void);
