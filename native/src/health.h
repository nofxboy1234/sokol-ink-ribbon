#pragma once
#include <stdbool.h>

// Grace's health as hit points. She starts with HEALTH_BASE_MAX; each
// Steroids dose raises the maximum by one and heals her fully. The
// Fine / Caution / Danger label is read off the points left.
#define HEALTH_BASE_MAX 3

void health_init(void);
int health_hp(void);
int health_max(void);
// 0 Fine, 1 Caution, 2 Danger (the label the HUD shows).
int health_state(void);
// Set by label: 0 full, 1 Caution, 2 one point left.
void health_set(int state);
// Lose one point; true if that was her last.
bool health_hurt(void);
void health_heal(int points);
void health_heal_full(void);
void health_raise_max(int points);
void health_restore(int hp, int max);
