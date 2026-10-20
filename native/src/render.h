#pragma once
#include "pathfind.h"

void render_init(void);
void render_shutdown(void);
#include <stdbool.h>

// Throw effects drawn on top of the scene for the coming frame.
typedef struct {
    bool aim;            // the straight aim line to the reticle
    float aim_wx, aim_wy;  // reticle world position (cells, float)
    bool aim_ok;         // in range with a clear line (cyan), else grey
    bool impact;         // the bottle's impact at a cell
    int impact_x, impact_y;
    float impact_pulse;  // 0..1 as the ring pulses out
    float impact_alpha;  // 1 while pulsing, then fades to 0
    bool aim_enemy;      // the aim is on the enemy: a round reticle at its real position
    float aim_ex, aim_ey;
    float grace_flash;   // seconds of damage flashing left on Grace
    float grace_stress;  // seconds of fast breathing left after a hit
    bool grace_waiting;  // slow breathing while she waits
    float clock;         // seconds, for animation
} render_fx_t;

void render_set_fx(const render_fx_t* fx);
void render_scene(int hover_x, int hover_y, const path_t* path, const path_t* route);

// Map streaming (stream.h): the map changed, so cached chunk geometry rebuilds.
void render_map_changed(void);
// Load the chunks around the current camera view now (after a camera jump).
void render_stream_prefetch(void);
// Load the chunks around a view centred on (cx, cy) cells, before a teleport.
void render_stream_prefetch_at(float cx, float cy);
// Debug: draw the chunk overlay; freeze the resident set (screenshots).
void render_stream_debug(bool overlay, bool freeze);
