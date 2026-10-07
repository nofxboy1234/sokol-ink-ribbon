#include "draw.h"
#include "sokol_gfx.h"
#include "sokol_gl.h"
#include <math.h>

void draw_quad(vec2_t a, vec2_t b, vec2_t c, vec2_t d) {
    sgl_v2f(a.x, a.y);
    sgl_v2f(b.x, b.y);
    sgl_v2f(c.x, c.y);
    sgl_v2f(d.x, d.y);
}

void draw_line(vec2_t a, vec2_t b, float width, float r, float g, float bl, float al) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) {
        return;
    }
    const float nx = -dy / len * width * 0.5f;
    const float ny = dx / len * width * 0.5f;
    sgl_c4f(r, g, bl, al);
    draw_quad(
        (vec2_t){ a.x + nx, a.y + ny },
        (vec2_t){ b.x + nx, b.y + ny },
        (vec2_t){ b.x - nx, b.y - ny },
        (vec2_t){ a.x - nx, a.y - ny });
}

// The reference frame's ramp: shadow navy -> deep navy -> mid blue -> sky ->
// cool white, with a very steep climb and only a thin band before white. The
// wide bands are dark and the core is hot, which is the opposite of a cyan
// halo, and it is what makes the walls read as lit surfaces instead of strokes
// drawn on top of the dark.
// Layer alphas are halved so the walls read as a faint glow. The bright bands
// are saturated blues rather than near-white: a white core blended over dark
// navy averages towards grey, which reads as slate rather than the reference's
// sky blue. The core lands just above the frame's sky blue (~140 luminance)
// and below its accent cyan (~179), so a picked-up item still outshines the
// walls and Grace still outshines both.
void draw_glow_line(vec2_t a, vec2_t b) {
    draw_line(a, b, 20.0f, 0.055f, 0.149f, 0.376f, 0.0500f);
    draw_line(a, b, 11.0f, 0.078f, 0.169f, 0.380f, 0.0800f);
    draw_line(a, b,  6.0f, 0.180f, 0.439f, 0.855f, 0.2200f);
    draw_line(a, b,  3.0f, 0.314f, 0.584f, 0.878f, 0.5000f);
    draw_line(a, b,  1.4f, 0.620f, 0.812f, 0.898f, 0.7000f);
}

void draw_glow_dot(vec2_t center) {
    const int segments = 48;
    const float PI = 3.14159265f;
    const float radii[5] = { 30.0f, 18.0f, 10.0f, 5.0f, 2.5f };
    const float center_a[5] = { 0.05f, 0.12f, 0.32f, 0.85f, 1.0f };
    const float rim_a[5] = { 0.0f, 0.0f, 0.0f, 0.20f, 0.55f };
    const float cr[5] = { 0.055f, 0.078f, 0.314f, 0.620f, 0.918f };
    const float cg[5] = { 0.149f, 0.169f, 0.584f, 0.878f, 0.988f };
    for (int layer = 0; layer < 5; layer++) {
        const float rad = radii[layer];
        for (int i = 0; i < segments; i++) {
            const float a0 = (float)i / (float)segments * 2.0f * PI;
            const float a1 = (float)(i + 1) / (float)segments * 2.0f * PI;
            sgl_c4f(cr[layer], cg[layer], 1.0f, center_a[layer]);
            sgl_v2f(center.x, center.y);
            sgl_c4f(cr[layer], cg[layer], 1.0f, rim_a[layer]);
            sgl_v2f(center.x + cosf(a0) * rad, center.y + sinf(a0) * rad);
            sgl_v2f(center.x + cosf(a1) * rad, center.y + sinf(a1) * rad);
        }
    }
}

void draw_filled_circle(vec2_t center, float radius, float r, float g, float b, float a) {
    const int segments = 32;
    const float PI = 3.14159265f;
    for (int i = 0; i < segments; i++) {
        const float a0 = (float)i / (float)segments * 2.0f * PI;
        const float a1 = (float)(i + 1) / (float)segments * 2.0f * PI;
        sgl_c4f(r, g, b, a);
        sgl_v2f(center.x, center.y);
        sgl_v2f(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius);
        sgl_v2f(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius);
    }
}
