#pragma once
#include <stdbool.h>
#include "vec2.h"

void render_init(void);
void render_shutdown(void);

// record the sokol_gl geometry for this frame (walls, hover cell, popup,
// item dots, player dot)
void render_scene(int hover_x, int hover_y);

// screen-space rect of the "Pick up" popup; true when it should be shown
bool render_popup_rect(vec2_t* min, vec2_t* max);

// draw the popup label (call inside the render pass, after sgl_draw)
void render_draw_overlay(void);