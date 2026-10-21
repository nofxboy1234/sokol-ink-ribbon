#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A prompt label centred on x with its bottom at y, with a black border:
// black text on white, or on red when alert. Draw-only (takes no input).
void game_label(const char* text, float x, float y, bool alert);

// Whether dear-imgui is taking mouse input this frame (tests: labels must not).
bool game_ui_wants_mouse(void);

#ifdef __cplusplus
}
#endif
