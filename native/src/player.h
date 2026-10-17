#pragma once
#include "pathfind.h"
#include <stdbool.h>

typedef enum {
    PLAYER_WALK = 0,
    PLAYER_RUN,
} player_move_t;

#define PLAYER_WALK_SPEED_CELLS 3.0f
#define PLAYER_RUN_SPEED_CELLS 6.5f

void player_init(int x, int y);
void player_set_cell(int x, int y);
void player_move_to(int x, int y, player_move_t mode);
// Realtime steering (WASD / analogue stick): a velocity in cells per second,
// already scaled for walk/run by the caller. Zero stops her. Collides against
// the grid (walls, doors, obstacles) and never crosses a climbable edge.
void player_set_velocity(float vx, float vy);
bool player_steering(void);
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

bool player_climbing(void);
void player_climb_to(int x, int y);
