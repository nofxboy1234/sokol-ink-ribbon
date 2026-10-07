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

void draw_glow_line(vec2_t a, vec2_t b) {
    draw_line(a, b, 20.0f, 0.15f, 0.45f, 1.00f, 0.045f);
    draw_line(a, b, 11.0f, 0.20f, 0.55f, 1.00f, 0.090f);
    draw_line(a, b,  6.0f, 0.28f, 0.65f, 1.00f, 0.180f);
    draw_line(a, b,  3.0f, 0.42f, 0.78f, 1.00f, 0.400f);
    draw_line(a, b,  1.4f, 0.70f, 0.92f, 1.00f, 0.850f);
}

void draw_glow_dot(vec2_t center) {
    const int segments = 48;
    const float PI = 3.14159265f;
    const float radii[5] = { 34.0f, 21.0f, 11.5f, 5.5f, 2.6f };
    const float center_a[5] = { 0.06f, 0.14f, 0.35f, 0.90f, 1.0f };
    const float rim_a[5] = { 0.0f, 0.0f, 0.0f, 0.20f, 0.55f };
    const float cr[5] = { 0.20f, 0.30f, 0.45f, 0.75f, 1.0f };
    const float cg[5] = { 0.50f, 0.62f, 0.78f, 0.92f, 1.0f };
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
