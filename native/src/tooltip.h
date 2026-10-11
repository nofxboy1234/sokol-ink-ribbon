#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Draw a black rectangle with white text at (x, y) using dear-imgui.
void game_tooltip(const char* text, float x, float y);

// Save/load menu (returns 0 = none, 1..n = save slot, 100+slot = load slot).
int game_save_menu(bool* open, const double* times, const unsigned char* used, int nslots);

#ifdef __cplusplus
}
#endif
