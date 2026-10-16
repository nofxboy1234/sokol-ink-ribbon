#pragma once
// The map palette (ref/map.kra), shared by the game (render.c) and the level
// editor (editor.cc) so both draw the level in the same colours.
#define PAL_PAPER 1.0f, 1.0f, 1.0f          // floor and the map background
#define PAL_INK 0.118f, 0.118f, 0.118f      // walls and outlines
#define PAL_GREEN 0.0f, 1.0f, 0.149f
#define PAL_CYAN 0.118f, 1.0f, 0.973f
#define PAL_YELLOW 0.949f, 1.0f, 0.0f
#define PAL_MAGENTA 1.0f, 0.0f, 0.416f
#define PAL_RED 1.0f, 0.0f, 0.165f
#define PAL_PURPLE 0.588f, 0.118f, 1.0f     // Grace (the editor's start marker)
#define PAL_BLUE 0.0f, 0.867f, 1.0f
#define PAL_GRAY 0.616f, 0.616f, 0.616f
#define PAL_WALL_PX 2.0f                    // wall line thickness
