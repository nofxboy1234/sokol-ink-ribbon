#pragma once
#include <stdbool.h>

typedef enum {
    PLAYER_WALK = 0,
    PLAYER_RUN,
} player_move_t;

void player_init(int x, int y);
void player_move_to(int x, int y, player_move_t mode);
void player_stop(void);
void player_update(float dt);

float player_x(void);
float player_y(void);
void player_cell(int* x, int* y);
int player_steps_taken(void);
bool player_is_moving(void);
bool player_is_waiting(void);
void player_toggle_wait(void);
player_move_t player_move_mode(void);
int player_facing(void);
