#pragma once

void render_init(void);
void render_shutdown(void);

// record the sokol_gl geometry for this frame (walls, hover cell, player dot)
void render_scene(int hover_x, int hover_y);
