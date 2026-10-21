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
    {
        // cut: void the cells, drop the objects and sections inside, clear links into it
        level_t cl;
        level_init(&cl);
        char e[256];
        int n = levelscript_run(&cl, "size 30 12\nroom 0 0 9 6 Keep\nroom 12 0 20 6 Gone\n"
                                     "obj stairs 2 2 name=up\nobj stairs 14 2 name=down link=up\nlink up down\n"
                                     "cut 11 0 21 7\n", e, sizeof(e));
        bool tiles_gone = true;
        for (int y = 0; y <= 7; y++)
            for (int x = 11; x <= 21; x++) tiles_gone &= level_tile(&cl, x, y) == TILE_VOID;
        const obj_t* up = find(&cl, OBJ_STAIRS, 2, 2);
        CHECK(n == 7 && tiles_gone && !find(&cl, OBJ_STAIRS, 14, 2) && up && up->link_id == 0 &&
                  cl.section_count == 1 && strcmp(cl.sections[0].name, "Keep") == 0,
              "cut voids its cells and drops the objects, sections and links inside it");
    }
    {
        // a one-sided door's bolt: set by script, kept through JSON
        char e[256];
        CHECK(levelscript_run(&lv, "set 9 2 lock=other_side state=locked side=-1 bolt=1\n", e, sizeof(e)) == 1,
              "bolt is a door property");
        level_to_json(&lv, json, sizeof(json));
        const obj_t* d = level_from_json(&back, json) ? find(&back, OBJ_DOOR, 9, 2) : NULL;
        CHECK(d && d->lock == LOCK_OTHER_SIDE && d->side == -1 && d->bolt == 1, "the bolt and its side round-trip");
        levelscript_run(&lv, "set 9 2 lock=none state=unlocked side=0 bolt=0\n", e, sizeof(e));
    }
    {
        // one Player Spawn per level: a second one moves it; spawns round-trip
        CHECK(run("obj player_spawn 3 4\nobj player_spawn 5 2\nobj enemy_spawn 14 3\n") == 3, "spawn objects place");
        int spawns = 0;
        for (int i = 0; i < lv.obj_count; i++) spawns += lv.objs[i].kind == OBJ_START;
        CHECK(spawns == 1 && find(&lv, OBJ_START, 5, 2) && lv.start_x == 5 && lv.start_y == 2,
              "a second Player Spawn moves the one Grace starts at");
        const obj_t* es = find(&lv, OBJ_ENEMY_SPAWN, 14, 3);
        CHECK(es != NULL, "enemy spawn placed at its cell");
        level_to_json(&lv, json, sizeof(json));
        CHECK(level_from_json(&back, json) && find(&back, OBJ_START, 5, 2) && back.start_x == 5 &&
                  find(&back, OBJ_ENEMY_SPAWN, 14, 3),
              "spawns survive a JSON round trip");
    }

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

    // doors snap to the doorway between two rooms' shared wall
    level_init(&lv);
    CHECK(run("size 20 24\nroom 0 0 9 6 West\nroom 9 0 19 6 East\ntile floor 9 2 9 3\ndoor 9 3\n") == 5,
          "fit door script runs");
    door = find(&lv, OBJ_DOOR, 9, 2);
    CHECK(door && !door->horizontal && door->span == 2 && level_door_flush(&lv, door),
          "door with no orientation snaps flush into the 2-cell gap");
    CHECK(run("room 0 12 19 22 Hall\ndoor 5 17\n") < 0, "door with no orientation fails in an open room");
    CHECK(run("door 14 6 h\n") == 1 && find(&lv, OBJ_DOOR, 14, 6)->span == DOOR_SPAN,
          "an oriented door with no span is a normal span-2 door");
    CHECK(run("room 0 6 9 11 South\ntile floor 3 6 5 6\ndoor 4 6 name=barrier\n") == 3 &&
              find(&lv, OBJ_DOOR, 3, 6) && find(&lv, OBJ_DOOR, 3, 6)->span == 3,
          "a fitted barrier keeps the width of its gap");
    if (!find(&lv, OBJ_DOOR, 3, 6)) {
        printf("FAILED %d\n", failures);
        return 1;
    }
    int barrier_id = find(&lv, OBJ_DOOR, 3, 6)->id;
    char line[64];
    CHECK(run("item 2 2 herb\n") == 1, "item for move");
    int herb_id = find(&lv, OBJ_ITEM, 2, 2)->id;
    snprintf(line, sizeof(line), "move %d 4 4\n", herb_id);
    CHECK(run(line) == 1 && find(&lv, OBJ_ITEM, 4, 4) && !find(&lv, OBJ_ITEM, 2, 2), "move carries an object");
    snprintf(line, sizeof(line), "delete %d\n", barrier_id);
    int before = lv.obj_count;
    CHECK(run(line) == 1 && lv.obj_count == before - 1 && !find(&lv, OBJ_DOOR, 3, 6), "delete removes one object");
    CHECK(run("delete 9999\n") < 0, "delete of a missing id fails");
    CHECK(run("movetile 0 0 1 1\n") == 1 && level_tile(&lv, 0, 0) == TILE_VOID &&
              level_tile(&lv, 1, 1) == TILE_CORNER_NW,
          "movetile carries the tile and leaves true void");

    char err[256];
    level_init(&lv);
    CHECK(levelscript_run(&lv, "size 10 10\nbogus 1 2\n", err, sizeof(err)) < 0 && strstr(err, "line 2"),
          "bad command reports its line");
    CHECK(levelscript_run(&lv, "tile wall 1 1\n", err, sizeof(err)) < 0, "unknown tile kind is rejected");

    printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
