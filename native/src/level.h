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
    OBJ_LIGHT,
    OBJ_SWITCH,
    OBJ_TYPEWRITER,
    OBJ_FILE,
    OBJ_SAFE,
    OBJ_OBSTACLE,
    OBJ_MOVABLE,
    OBJ_OPENABLE,
    OBJ_START,
    OBJ_GOAL,
    OBJ_KIND_COUNT,
} obj_kind_t;

typedef enum {
    ITEM_BOTTLE = 0,
    ITEM_COIN,
    ITEM_HERB,
    ITEM_INK_RIBBON,
    ITEM_SCREWDRIVER,
    ITEM_INJECTOR,
    ITEM_FUSE,
    ITEM_LIGHTER,
    ITEM_CHERUB_KEY,
    ITEM_COUNT,
} item_t;

typedef enum {
    DOOR_UNKNOWN = 0,
    DOOR_LOCKED,
    DOOR_UNLOCKED,
    DOOR_UNOPENABLE,
} door_state_t;

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
    int horizontal;
    int state;
    int open;
    int key_id;
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
item_t item_from_name(const char* name);
const char* door_state_name(int state);
int door_state_from_name(const char* name);

#ifdef __cplusplus
}
#endif
