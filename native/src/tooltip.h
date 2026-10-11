#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Draw a black rectangle with white text at (x, y) using dear-imgui.
void game_tooltip(const char* text, float x, float y);

#ifdef __cplusplus
}
#endif
