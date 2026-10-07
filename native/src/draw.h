#pragma once
#include "vec2.h"

void draw_quad(vec2_t a, vec2_t b, vec2_t c, vec2_t d);

// a straight segment rendered as a quad of the given screen-space width
void draw_line(vec2_t a, vec2_t b, float width, float r, float g, float bl, float al);

// a soft, low-opacity glowing blue line (layered quads fake a bloom)
void draw_glow_line(vec2_t a, vec2_t b);

// a glowing blue dot (bright core with a soft halo)
void draw_glow_dot(vec2_t center);
