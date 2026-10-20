#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEVEL_MAX_WALLS 4096
#define LEVEL_MAX_SECTIONS 128
#define LEVEL_MAX_OBJECTS 1024
#define LEVEL_MAX_NAME 32
#define LEVEL_MAX_COLS 256
#define LEVEL_MAX_ROWS 256

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
    OBJ_START,
    // a marker for where the level ends (the game itself ignores it)
    OBJ_GOAL,
    OBJ_RECIPE,
    OBJ_CABINET,
    // a w*h stairwell; stepping onto it moves Grace to the linked stairs
    OBJ_STAIRS,
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

// One cell of the tile map. Every tile except floor is solid; the four
// corners and the two straight walls differ only in how they draw.
typedef enum {
    TILE_VOID = 0,
    TILE_FLOOR,
    TILE_CORNER_NW,
    TILE_CORNER_NE,
    TILE_CORNER_SW,
    TILE_CORNER_SE,
    TILE_WALL_H,
    TILE_WALL_V,
    TILE_KIND_COUNT,
} tile_t;

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
    int movable;   // obstacle property: a pushable-looking block (sight blocker)
    int openable;  // obstacle property: a container that opens (uses open/link)
    int code;
    char name[LEVEL_MAX_NAME];
} obj_t;

typedef struct {
    char name[LEVEL_MAX_NAME];
    int cols, rows;
    int start_x, start_y;
    // has_tiles: the tile map is the source of truth and walls are derived
    int has_tiles;
    uint8_t tiles[LEVEL_MAX_ROWS * LEVEL_MAX_COLS];
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

const obj_t* level_obj_by_id(const level_t* lv, int id);
const obj_t* level_stairs_at(const level_t* lv, int x, int y);

tile_t level_tile(const level_t* lv, int x, int y);
void level_set_tile(level_t* lv, int x, int y, tile_t t);
// Rebuild walls[] from the tile map: a wall edge wherever floor meets a
// non-floor tile or the map border.
void level_compile_tiles(level_t* lv);

// A normal door is two cells wide (Room 202's door is the reference).
// Barriers and gates are a separate kind of door and keep their own span.
#define DOOR_SPAN 2
// The widest gap a doorway fit accepts (wider is an open room, not a door).
#define DOOR_FIT_MAX_GAP 6

// Snap a door to the doorway at or next to cell (x, y): the run of floor
// cells through a wall band, between solid jamb cells, with floor on both
// sides. The door sits on the band's top (h) or left (v) wall line, the way
// the game draws and collides it. Returns false when there is no doorway.
bool level_door_fit(const level_t* lv, int x, int y, int* ox, int* oy, int* horizontal, int* span);
// True when the door fills its doorway exactly: both ends touch a jamb and it
// lies on the band's wall line (what level_door_fit would place).
bool level_door_flush(const level_t* lv, const obj_t* door);
char tile_glyph(tile_t t);
tile_t tile_from_glyph(char c);
const char* tile_name(tile_t t);
tile_t tile_from_name(const char* name);
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
