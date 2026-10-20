#include "../src/level.h"
#include "../src/levelscript.h"
#include "../src/grid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg)                                 \
    do {                                                 \
        if (cond) {                                      \
            printf("PASS %s\n", msg);                    \
        } else {                                         \
            printf("FAIL %s\n", msg);                    \
            failures++;                                  \
        }                                                \
    } while (0)

static level_t lv, back;
static char json[4 * 1024 * 1024];

static const obj_t* find(const level_t* l, obj_kind_t kind, int x, int y) {
    for (int i = 0; i < l->obj_count; i++)
        if (l->objs[i].kind == kind && l->objs[i].x == x && l->objs[i].y == y) return &l->objs[i];
    return NULL;
}

static int run(const char* s) {
    char err[256];
    int n = levelscript_run(&lv, s, err, sizeof(err));
    if (n < 0) printf("  script error: %s\n", err);
    return n;
}

int main(void) {
    level_init(&lv);
    CHECK(run("size 20 12\n"
              "room 0 0 9 6 West Room\n"
              "room 9 0 19 6 East Room\n"
              "door 9 2 v 2 state=locked name=east_door\n"
              "item 3 3 fuse name=the_fuse\n"
              "obj fusebox 15 3 link=the_fuse\n"
              "start 2 2\n") == 7, "script runs every command");
    CHECK(level_tile(&lv, 0, 0) == TILE_CORNER_NW && level_tile(&lv, 19, 6) == TILE_CORNER_SE, "room places corner tiles");
    CHECK(level_tile(&lv, 4, 0) == TILE_WALL_H && level_tile(&lv, 0, 3) == TILE_WALL_V, "room places straight walls");
    CHECK(level_tile(&lv, 9, 2) == TILE_FLOOR && level_tile(&lv, 9, 3) == TILE_FLOOR, "door cells become floor");
    const obj_t* door = find(&lv, OBJ_DOOR, 9, 2);
    CHECK(door && !door->horizontal && door->span == 2 && door->state == DOOR_LOCKED, "door object carries its props");
    const obj_t* fuse = find(&lv, OBJ_ITEM, 3, 3);
    const obj_t* box = find(&lv, OBJ_FUSEBOX, 15, 3);
    CHECK(fuse && fuse->item_type == ITEM_FUSE, "item placed at its cell");
    CHECK(box && fuse && box->link_id == fuse->id, "link resolves by name");
    char err2[256];
    CHECK(levelscript_run(&lv, "set 9 2 state=unlocked name=gate\n", err2, sizeof(err2)) == 1 &&
              find(&lv, OBJ_DOOR, 9, 2)->state == DOOR_UNLOCKED && strcmp(find(&lv, OBJ_DOOR, 9, 2)->name, "gate") == 0,
          "set changes the object on a cell");
    CHECK(lv.section_count == 2 && strcmp(lv.sections[1].name, "East Room") == 0, "named rooms become sections");

    grid_init(&lv);
    CHECK(grid_is_floor(2, 2) && grid_is_floor(15, 3), "both rooms walkable through the door gap");
    CHECK(!grid_is_floor(0, 0) && !grid_is_floor(4, 0), "wall tiles are solid");
    CHECK(grid_blocked(1, 1, 1, 0), "floor next to a wall tile is blocked");

    int n = level_to_json(&lv, json, sizeof(json));
    CHECK(n > 0 && strstr(json, "\"tiles\"") && !strstr(json, "\"walls\""), "save writes tiles, not walls");
    CHECK(level_from_json(&back, json) && memcmp(back.tiles, lv.tiles, sizeof(lv.tiles)) == 0 &&
              back.wall_count == lv.wall_count, "tiles round-trip and recompile the same walls");

    level_init(&lv);
    CHECK(run("size 12 8\nfloor 2 2 9 5\nautowall\n") == 3, "autowall script runs");
    CHECK(level_tile(&lv, 1, 1) == TILE_CORNER_NW && level_tile(&lv, 10, 1) == TILE_CORNER_NE &&
              level_tile(&lv, 1, 6) == TILE_CORNER_SW && level_tile(&lv, 10, 6) == TILE_CORNER_SE,
          "autowall picks the four corners");
    CHECK(level_tile(&lv, 5, 1) == TILE_WALL_H && level_tile(&lv, 1, 3) == TILE_WALL_V, "autowall picks straight walls");

    level_init(&lv);
    CHECK(run("size 30 10\nroom 0 0 6 6\nroom 20 0 26 6\n"
              "obj stairs 2 2 w=2 h=2 name=up\nobj stairs 22 2 w=2 h=2 name=down link=up\nlink up down\nstart 4 4\n") > 0,
          "stairs script runs");
    grid_init(&lv);
    const obj_t* up = find(&lv, OBJ_STAIRS, 2, 2);
    const obj_t* dn = level_stairs_at(&lv, 23, 3);
    CHECK(up && dn && level_obj_by_id(&lv, up->link_id) == dn && level_obj_by_id(&lv, dn->link_id) == up,
          "stairs link both ways");
    CHECK(grid_is_floor(22, 2), "a floor reached only by stairs is walkable");

    level_init(&lv);
    CHECK(run("size 20 10\nroom 0 0 19 9\nobj light 5 5 name=far\nobj light 9 4 name=near\n"
              "setnear light 10 5 3 breakable=1\n") > 0, "setnear script runs");
    CHECK(find(&lv, OBJ_LIGHT, 9, 4)->breakable && !find(&lv, OBJ_LIGHT, 5, 5)->breakable,
          "setnear changes only the nearest object of its kind");
    CHECK(run("setnear light 15 5 2 breakable=1\n") < 0, "setnear fails with nothing in range");

    level_init(&lv);
    CHECK(run("size 20 10\nsection 2 2 4 4 A\ncover 6 3\ncover 1 2\n") > 0, "cover script runs");
    CHECK(lv.sections[0].x == 1 && lv.sections[0].y == 2 && lv.sections[0].w == 6 && lv.sections[0].h == 4,
          "cover grows the neighbouring section to the cell");
    CHECK(run("cover 15 8\n") < 0, "cover fails with no section next to the cell");

    char err[256];
    level_init(&lv);
    CHECK(levelscript_run(&lv, "size 10 10\nbogus 1 2\n", err, sizeof(err)) < 0 && strstr(err, "line 2"),
          "bad command reports its line");
    CHECK(levelscript_run(&lv, "tile wall 1 1\n", err, sizeof(err)) < 0, "unknown tile kind is rejected");

    printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
