#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEVEL_MAX_WALLS 4096
#define LEVEL_MAX_SECTIONS 64
#define LEVEL_MAX_OBJECTS 1024
#define LEVEL_MAX_NAME 32

typedef enum {
    OBJ_DOOR = 0,
    OBJ_ITEM,
    OBJ_FUSEBOX,
    OBJ_LIGHT,
    OBJ_SWITCH,
    OBJ_TYPEWRITER,
    OBJ_FILE,
    OBJ_SAFE,
    OBJ_OBSTACLE,
    OBJ_MOVABLE,
    OBJ_OPENABLE,
    OBJ_START,
    // a marker for where the level ends (the game itself ignores it)
    OBJ_GOAL,
    OBJ_KIND_COUNT,
} obj_kind_t;

typedef enum {
    ITEM_BOTTLE = 0,
    ITEM_COIN = 1,
    ITEM_HERB = 2,
    ITEM_INK_RIBBON = 3,
    ITEM_SCREWDRIVER = 4,
    ITEM_INJECTOR = 5,
    ITEM_FUSE = 6,
    ITEM_LIGHTER = 7,
    ITEM_CHERUB_KEY = 8,
    ITEM_MIXED_HERB = 9,
    ITEM_COUNT = 10,
} item_t;

#ifdef __cplusplus
#define _Static_assert(cond, msg) static_assert(cond, msg)
#endif
_Static_assert(ITEM_HERB == 2, "save ordinal");
_Static_assert(ITEM_LIGHTER == 7, "save ordinal");
_Static_assert(ITEM_CHERUB_KEY == 8, "save ordinal");
_Static_assert(ITEM_MIXED_HERB == 9, "save ordinal");
_Static_assert(ITEM_COUNT == 10, "save ordinal");
#ifdef __cplusplus
#undef _Static_assert
#endif

typedef enum {
    DOOR_UNKNOWN = 0,
    DOOR_LOCKED,
    DOOR_UNLOCKED,
    // smashed by the enemy: the slab is gone and the gap stays open
    DOOR_BROKEN,
} door_state_t;

// A level light's condition, stored in obj_t.state. Once broken it never
// lights again, so 0 and 1 keep the meaning of the old bool.
typedef enum {
    LIGHT_OFF = 0,
    LIGHT_ON = 1,
    LIGHT_BROKEN = 2,
} light_state_t;

typedef struct {
    int x0, y0, x1, y1;
} wall_seg_t;

typedef struct {
    int x, y, w, h;
    char name[LEVEL_MAX_NAME];
} section_t;

typedef struct {
    int id;
    obj_kind_t kind;
    int x, y;
    int w, h;
    int horizontal;
    int span;
    int state;
    int discover_state;
    int open;
    int key_id;
    int link_id;
    float auto_close;
    int breakable;
    int item_type;
    int group_id;
    float radius;
    int climbable;
    int code;
    char name[LEVEL_MAX_NAME];
} obj_t;

typedef struct {
    char name[LEVEL_MAX_NAME];
    int cols, rows;
    int start_x, start_y;
    int wall_count;
    wall_seg_t walls[LEVEL_MAX_WALLS];
    int section_count;
    section_t sections[LEVEL_MAX_SECTIONS];
    int obj_count;
    obj_t objs[LEVEL_MAX_OBJECTS];
} level_t;

void level_init(level_t* lv);
bool level_from_json(level_t* lv, const char* json);
int level_to_json(const level_t* lv, char* out, int cap);

const char* obj_kind_name(obj_kind_t kind);
obj_kind_t obj_kind_from_name(const char* name);
const char* item_name(item_t item);
const char* item_display_name(int item);
item_t item_from_name(const char* name);
const char* door_state_name(int state);
int door_state_from_name(const char* name);

// Reference map colours, shared by the runtime and the editor.
void item_color_rgb(int item, float* r, float* g, float* b);
const char* item_color_hex(int item);
int item_icon_shape(int item);
void door_color_rgb(int state, float* r, float* g, float* b);

#ifdef __cplusplus
}
#endif
