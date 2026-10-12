#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Draw a label at (x, y). alert uses a red rectangle with black text;
// otherwise the rectangle is black and the text is white.
void game_tooltip(const char* text, float x, float y, bool alert);

#ifdef __cplusplus
}
#endif
