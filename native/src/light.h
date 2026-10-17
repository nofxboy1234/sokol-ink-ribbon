#pragma once
#include <stdbool.h>

#include "level.h"

// Light and visibility. A light source is Grace with the Lighter lit, or a
// level light that is switched on. Light reaches cells within its radius
// that it has a clear line to (walls and closed doors cast shadows, like
// sight). Items are only drawn, hovered and picked up on lit cells (or once
// the Lighter has shown them); Grace,
// the enemy, doors and other objects stay visible.
#define LIGHTER_RADIUS_CELLS 4.0f     // the Lighter's circle around Grace
#define LIGHT_DEFAULT_RADIUS 5.0f     // a level light with no radius set
#define LIGHT_RADIUS_MIN 1.0f         // editor limits for a light's radius
#define LIGHT_RADIUS_MAX 16.0f
#define LIGHT_OVERLAY_ALPHA 0.22f     // the yellow wash over lit cells

// Recomputes the lit cells from every active source.
void light_update(void);
bool light_cell_lit(int x, int y);
// An item is on the map if it is lit now or the Lighter has shown it before.
bool light_item_visible(int object_index);
// The radius a level light shines with (its own, or the default).
float light_radius_of(float radius);

// A lamp's condition lives in obj_t.state; read it here so a broken lamp (2)
// is never taken for a lit one.
bool light_is_on(const obj_t* o);
bool light_is_broken(const obj_t* o);
// A lit, breakable lamp; an off or already broken one is not a target.
bool light_is_breakable_now(const obj_t* o);
// Index of the nearest breakable lamp within r cells of (x, y), else -1. A wall
// or a closed door between the cell and the lamp blocks the smash. Ties keep
// the lowest object index.
int light_breakable_near(float x, float y, float r);
// Off to on or on to off. A broken lamp stays broken.
void light_toggle(int index);
// Smashes a breakable lamp. Does nothing to anything else, so it is safe to repeat.
void light_break(int index);
int light_broken_count(void);
