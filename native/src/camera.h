#pragma once
#include <stdbool.h>

void camera_init(void);
void camera_set_viewport(int w, int h);
void camera_follow(float cx, float cy);
void camera_recentre(void);
void camera_update(float dt);
void camera_pan_pixels(float dx, float dy);
void camera_zoom_at(float screen_x, float screen_y, float factor);
void camera_zoom_step(float screen_x, float screen_y, float steps);
void camera_set_follow(bool on);
bool camera_following(void);

float camera_cell_px(void);
float camera_center_x(void);
float camera_center_y(void);
void camera_screen_to_cell(float sx, float sy, int* cx, int* cy);
void camera_cell_to_screen(float cx, float cy, float* sx, float* sy);
