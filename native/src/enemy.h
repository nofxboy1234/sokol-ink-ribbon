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
    ENEMY_WAIT,         // just hurt Grace: stands still, senses nothing
    ENEMY_INVESTIGATE,  // going to look at and around a bottle's impact
} enemy_state_t;

// The whole enemy (position, state, timers, anger, route) for save slots.
#define ENEMY_SAVE_BYTES 40000
typedef struct {
    unsigned char bytes[ENEMY_SAVE_BYTES];
} enemy_save_t;
void enemy_export(enemy_save_t* out);
void enemy_import(const enemy_save_t* in);

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
// After hurting Grace: it waits this long, then searches.
void enemy_rest(float seconds);
// Sight needs Grace to be lit (her Lighter on, or inside a level light);
// in the dark she can only be heard or smelt.
void enemy_set_grace_lit(bool lit);
// A bottle hit: stunned and flashing for the given seconds.
void enemy_damage(float seconds);
// Seconds of damage flashing left (0 when not flashing).
float enemy_flash(void);
// Seconds of anger left after a hit (0 when calm).
float enemy_angry(void);
// A smashed bottle at a cell carrying radius cells: the top priority. It goes
// to look at and around the impact unless it is stunned, winded, waiting,
// smashing a lamp or door, or chasing Grace with her in sight. True if it went.
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
