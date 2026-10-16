#pragma once
#include <stdbool.h>

// Light and visibility. A light source is Grace with the Lighter lit, or a
// level light that is switched on. Light reaches cells within its radius
// that it has a clear line to (walls and closed doors cast shadows, like
// sight). Items are only drawn, hovered and picked up on lit cells; Grace,
// the enemy, doors and other objects stay visible.
#define LIGHTER_RADIUS_CELLS 4.0f     // the Lighter's circle around Grace
#define LIGHT_DEFAULT_RADIUS 5.0f     // a level light with no radius set
#define LIGHT_RADIUS_MIN 1.0f         // editor limits for a light's radius
#define LIGHT_RADIUS_MAX 16.0f
#define LIGHT_OVERLAY_ALPHA 0.22f     // the yellow wash over lit cells

// Recomputes the lit cells from every active source.
void light_update(void);
bool light_cell_lit(int x, int y);
// The radius a level light shines with (its own, or the default).
float light_radius_of(float radius);
