#pragma once
#include "pathfind.h"
#include <stdbool.h>

typedef enum {
    PLAYER_WALK = 0,
    PLAYER_RUN,
} player_move_t;

void player_init(int x, int y);
void player_set_cell(int x, int y);
void player_move_to(int x, int y, player_move_t mode);
const path_t* player_path(void);
void player_stop(void);
// Stop and settle on the nearest cell centre.
void player_halt(void);
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

int player_replay_length(void);
int player_replay_x(int index);
int player_replay_y(int index);

void player_note_time(int ms);
bool player_on_top(void);
int player_replay_t(int index);
