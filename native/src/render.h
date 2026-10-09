#pragma once
#include "pathfind.h"

void render_init(void);
void render_shutdown(void);
void render_scene(int hover_x, int hover_y, const path_t* path);
