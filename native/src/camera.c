#include "camera.h"

#include <math.h>

#define CAMERA_MIN_SCALE 6.0f
#define CAMERA_MAX_SCALE 96.0f
#define CAMERA_FIT_CELLS 28.0f
#define CAMERA_CATCHUP 6.0f

static struct {
    float center_x, center_y;
    float target_x, target_y;
    float scale;
    int width, height;
    bool following;
} cam;

void camera_init(void) {
    cam.center_x = cam.target_x = 0.0f;
    cam.center_y = cam.target_y = 0.0f;
    cam.scale = 0.0f;
    cam.width = 1;
    cam.height = 1;
    cam.following = true;
}

void camera_set_viewport(int w, int h) {
    cam.width = w > 1 ? w : 1;
    cam.height = h > 1 ? h : 1;
}

static float fit_scale(void) {
    float min_dim = (float)(cam.width < cam.height ? cam.width : cam.height);
    return min_dim / CAMERA_FIT_CELLS;
}

void camera_follow(float cx, float cy) {
    if (!cam.following) {
        return;
    }
    cam.target_x = cx;
    cam.target_y = cy;
}

void camera_recentre(void) {
    cam.center_x = cam.target_x;
    cam.center_y = cam.target_y;
}

void camera_update(float dt) {
    if (cam.scale <= 0.0f) {
        cam.scale = fit_scale();
    }
    float t = 1.0f - expf(-CAMERA_CATCHUP * dt);
    cam.center_x += (cam.target_x - cam.center_x) * t;
    cam.center_y += (cam.target_y - cam.center_y) * t;
}

void camera_pan_pixels(float dx, float dy) {
    cam.following = false;
    cam.center_x -= dx / cam.scale;
    cam.center_y -= dy / cam.scale;
    cam.target_x = cam.center_x;
    cam.target_y = cam.center_y;
}

void camera_zoom_at(float screen_x, float screen_y, float factor) {
    // keep the point under the cursor fixed while scaling
    float wx = (screen_x - cam.width * 0.5f) / cam.scale + cam.center_x;
    float wy = (screen_y - cam.height * 0.5f) / cam.scale + cam.center_y;
    float next = cam.scale * factor;
    if (next < CAMERA_MIN_SCALE) {
        next = CAMERA_MIN_SCALE;
    }
    if (next > CAMERA_MAX_SCALE) {
        next = CAMERA_MAX_SCALE;
    }
    cam.scale = next;
    cam.center_x = wx - (screen_x - cam.width * 0.5f) / cam.scale;
    cam.center_y = wy - (screen_y - cam.height * 0.5f) / cam.scale;
    cam.target_x = cam.center_x;
    cam.target_y = cam.center_y;
}

void camera_zoom_step(float screen_x, float screen_y, float steps) {
    camera_zoom_at(screen_x, screen_y, powf(1.1f, steps));
}

void camera_set_follow(bool on) {
    cam.following = on;
}

bool camera_following(void) {
    return cam.following;
}

float camera_cell_px(void) {
    return cam.scale;
}

float camera_center_x(void) {
    return cam.center_x;
}

float camera_center_y(void) {
    return cam.center_y;
}

void camera_screen_to_cell(float sx, float sy, int* cx, int* cy) {
    float wx = (sx - cam.width * 0.5f) / cam.scale + cam.center_x;
    float wy = (sy - cam.height * 0.5f) / cam.scale + cam.center_y;
    if (cx) {
        *cx = (int)floorf(wx);
    }
    if (cy) {
        *cy = (int)floorf(wy);
    }
}

void camera_cell_to_screen(float cx, float cy, float* sx, float* sy) {
    if (sx) {
        *sx = (cx - cam.center_x) * cam.scale + cam.width * 0.5f;
    }
    if (sy) {
        *sy = (cy - cam.center_y) * cam.scale + cam.height * 0.5f;
    }
}
