#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Draw a label at (x, y). alert uses a red rectangle with black text;
// otherwise the rectangle is black and the text is white.
void game_tooltip(const char* text, float x, float y, bool alert);

// An item's name label centred on x with its bottom at y: black text on a
// white rectangle with a black border. Draw-only (takes no input).
void game_label(const char* text, float x, float y);

// Whether dear-imgui is taking mouse input this frame (tests: labels must not).
bool game_ui_wants_mouse(void);

#ifdef __cplusplus
}
#endif
