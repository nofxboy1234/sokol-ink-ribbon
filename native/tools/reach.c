// Reachability: play the level's progression to a fixpoint with the game's
// own rules and report what Grace can never reach.
//
//   reach LEVEL.json [--sides OUT.txt]
//
// Each round flood-fills the floor Grace can walk to (walls, closed doors,
// solid obstacles and stairs links as in the game), picks up every item in
// reach, opens every lock she now has the key for (doors, drawers, puzzle
// boxes, the fuse box), analyses specimens, and repeats until nothing
// changes. --sides writes `set X Y side=..` for each one-sided lock whose
// open side the run can decide: the side she can't reach without it.
#include "doors.h"
#include "gear.h"
#include "grid.h"
#include "interact.h"
#include "inventory.h"
#include "items.h"
#include "level.h"
#include "light.h"
#include "player.h"
#include "enemy.h"
#include "throw.h"
#include "health.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static level_t level;
static unsigned char reach[LEVEL_MAX_ROWS][LEVEL_MAX_COLS];
static unsigned char touched[LEVEL_MAX_OBJECTS];  // items she got to (taken or left behind)
static int peak_slots, overflow;

// Progression items she must carry; anything else may be left behind or
// dropped to make room (the game has no Item Box yet).
static bool opens_a_lock(int item) {
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if ((o->kind == OBJ_DOOR || o->kind == OBJ_OBSTACLE) &&
            (o->key_id == item || o->key2_id == item || o->key3_id == item))
            return true;
    }
    return false;
}

static bool essential(int item) {
    if (opens_a_lock(item) && item != ITEM_LOCKPICK) return true;
    item_cat_t c = item_category(item);
    return c == ICAT_KEY && item != ITEM_COIN && item != ITEM_INK_RIBBON && item != ITEM_LOCKPICK ? true
         : c == ICAT_TOOL || c == ICAT_POUCH || c == ICAT_GUN;  // she keeps her guns
}

// A full bag: drop the first slot that isn't essential.
static bool make_room(void) {
    for (int s = 0; s < inventory_capacity(); s++) {
        int it = inventory_slot(s);
        if (it >= 0 && !essential(it)) return inventory_clear_slot(s);
    }
    return false;
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
    buf[n] = 0;
    fclose(f);
    return buf;
}

static int floor_of(int y) { return y < 60 ? 3 : y < 142 ? 2 : 1; }

static bool walkable(int x, int y) {
    return grid_in_bounds(x, y) && grid_is_floor(x, y) && !obstacles_block_cell(x, y);
}

static int qx[LEVEL_MAX_ROWS * LEVEL_MAX_COLS], qy[LEVEL_MAX_ROWS * LEVEL_MAX_COLS];

static int flood(void) {
    memset(reach, 0, sizeof(reach));
    int head = 0, tail = 0, count = 0;
    qx[tail] = level.start_x;
    qy[tail++] = level.start_y;
    reach[level.start_y][level.start_x] = 1;
    while (head < tail) {
        int x = qx[head], y = qy[head++];
        count++;
        static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (!walkable(nx, ny) || reach[ny][nx] || grid_blocked(x, y, nx, ny) || doors_block_edge(x, y, nx, ny)) continue;
            reach[ny][nx] = 1;
            qx[tail] = nx;
            qy[tail++] = ny;
        }
        // stairs carry her to the linked flight
        const obj_t* s = level_stairs_at(&level, x, y);
        const obj_t* t = s && s->link_id > 0 ? level_obj_by_id(&level, s->link_id) : 0;
        if (t) {
            for (int yy = t->y; yy < t->y + t->h; yy++)
                for (int xx = t->x; xx < t->x + t->w; xx++)
                    if (walkable(xx, yy) && !reach[yy][xx]) {
                        reach[yy][xx] = 1;
                        qx[tail] = xx;
                        qy[tail++] = yy;
                    }
        }
    }
    return count;
}

static bool reached(int x, int y) {
    return grid_in_bounds(x, y) && reach[y][x];
}

// A cell she can touch: her own reachable cell or a reachable 4-neighbour
// with no wall between (items on tables, fixtures against walls).
static bool touchable(int x, int y, int* fx, int* fy) {
    static const int d[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int k = 0; k < 5; k++) {
        int nx = x + d[k][0], ny = y + d[k][1];
        if (reached(nx, ny) && (k == 0 || !grid_blocked(nx, ny, x, y))) {
            *fx = nx;
            *fy = ny;
            return true;
        }
    }
    return false;
}

static bool rect_touchable(const obj_t* o, int* fx, int* fy) {
    int w = o->w > 0 ? o->w : 1, h = o->h > 0 ? o->h : 1;
    for (int y = o->y; y < o->y + h; y++)
        for (int x = o->x; x < o->x + w; x++)
            if (touchable(x, y, fx, fy)) return true;
    return false;
}

// The two cells beside door slab k: side -1 (above / left) and +1.
static void door_sides(const obj_t* o, int k, int* ax, int* ay, int* bx, int* by) {
    if (o->horizontal) {
        *ax = *bx = o->x + k;
        *ay = o->y - 1;
        *by = o->y;
    } else {
        *ay = *by = o->y + k;
        *ax = o->x - 1;
        *bx = o->x;
    }
}

static int side_reached(const obj_t* o, int which) {
    int span = o->span > 0 ? o->span : 1;
    for (int k = 0; k < span; k++) {
        int ax, ay, bx, by;
        door_sides(o, k, &ax, &ay, &bx, &by);
        if (which < 0 ? reached(ax, ay) : reached(bx, by)) return 1;
    }
    return 0;
}

static bool round_step(int round, FILE* log) {
    bool changed = false;
    for (int i = 0; i < level.obj_count; i++) {
        obj_t* o = &level.objs[i];
        int fx, fy;
        if (o->kind == OBJ_ITEM && !items_taken(i) && !openable_covers_closed(o->x, o->y) && touchable(o->x, o->y, &fx, &fy)) {
            if (!touched[i]) {
                touched[i] = 1;
                changed = true;
            }
            // she carries what progression needs; counted pickups (ammo,
            // blood, scrap) take no slot; the rest she can leave behind
            bool ok = false;
            if (essential(o->item_type) || gear_counted(o->item_type)) {
                ok = items_take(i);
                if (!ok && make_room()) ok = items_take(i);
                if (!ok && !gear_counted(o->item_type)) overflow++;
            }
            if (inventory_count() > peak_slots) peak_slots = inventory_count();
            if (ok) {
                changed = true;
                item_cat_t c = item_category(o->item_type);
                if (log && (c == ICAT_KEY || c == ICAT_TOOL || c == ICAT_GUN || c == ICAT_MELEE || c == ICAT_POUCH))
                    fprintf(log, "  round %d: take %-28s at %3d,%3d (F%d)\n", round, item_display_name(o->item_type), o->x, o->y, floor_of(o->y));
            }
        } else if (((o->kind == OBJ_OBSTACLE && o->openable) || o->kind == OBJ_SAFE || o->kind == OBJ_MICROSCOPE ||
                    o->kind == OBJ_FUSEBOX) && rect_touchable(o, &fx, &fy)) {
            if (o->kind == OBJ_MICROSCOPE) {
                if (gear_analyse_specimen()) {
                    changed = true;
                    if (log) fprintf(log, "  round %d: analyse a specimen at %d,%d\n", round, o->x, o->y);
                }
                continue;
            }
            if (o->open || (o->kind == OBJ_FUSEBOX && !inventory_has(ITEM_FUSE))) continue;
            player_set_cell(fx, fy);
            bool was_open = o->open;
            bool done = interact_at(o->x, o->y);
            if (o->kind == OBJ_SAFE && interact_pending_safe() == i) {
                done = interact_try_safe_code(o->code);  // the code is in a file she has read
            }
            if (done && (o->open != was_open || o->kind == OBJ_FUSEBOX)) {
                changed = true;
                if (log) fprintf(log, "  round %d: open %-28s at %3d,%3d (F%d)\n", round, o->name, o->x, o->y, floor_of(o->y));
            }
        } else if (o->kind == OBJ_CABINET && !o->state && rect_touchable(o, &fx, &fy) &&
                   gear_count(ITEM_COIN) >= (o->code > 0 ? o->code : 1)) {
            player_set_cell(fx, fy);
            interact_at(o->x, o->y);
            if (o->state) {
                changed = true;
                if (log) fprintf(log, "  round %d: buy %-29s at %3d,%3d (F%d) for %d coins\n", round,
                                 o->qty > 0 ? item_display_name(o->item_type) : "+4 slots", o->x, o->y, floor_of(o->y), o->code);
            }
        } else if (o->kind == OBJ_GOAL && goal_met() && !level_complete() && reached(o->x, o->y)) {
            player_set_cell(o->x, o->y);
            if (interact_check_complete()) {
                changed = true;
                if (log) fprintf(log, "  round %d: reach the goal at %d,%d: Leon's Requiem (%d round loaded)\n", round,
                                 o->x, o->y, gear_loaded(ITEM_GUN_REQUIEM));
            }
        } else if (o->kind == OBJ_DOOR && o->state == DOOR_LOCKED) {
            // try from each reached cell beside the slab (a one-sided lock
            // with no side picked yet stays shut)
            int span = o->span > 0 ? o->span : 1;
            bool done = o->lock == LOCK_OTHER_SIDE && o->side == 0;
            for (int k = 0; k < span && !done; k++) {
                int c[4];
                door_sides(o, k, &c[0], &c[1], &c[2], &c[3]);
                for (int j = 0; j < 4 && !done; j += 2) {
                    if (reached(c[j], c[j + 1]) && doors_unlockable(o, c[j], c[j + 1])) {
                        doors_unlock(i);
                        done = changed = true;
                        if (log) fprintf(log, "  round %d: unlock %-26s at %3d,%3d (F%d)\n", round, o->name, o->x, o->y, floor_of(o->y));
                    }
                }
            }
        }
    }
    return changed;
}

static const char* lock_reason(const obj_t* o) {
    static char buf[128];
    switch (o->lock) {
        case LOCK_JAMMED: return "jammed (never opens for Grace)";
        case LOCK_OTHER_SIDE: return o->side ? "locked from the other side, which she never reaches" : "one-sided, side unknown";
        default: {
            int key = doors_key_type(o);
            snprintf(buf, sizeof(buf), "needs %s%s%s", key >= 0 ? item_display_name(key) : "a key",
                     o->key2_id > 0 ? " + " : "", o->key2_id > 0 ? item_display_name(o->key2_id) : "");
            return buf;
        }
    }
}

static int rounds;

// Load the level fresh, apply one-sided lock sides (side[i] for object i,
// 0 = keep), and play to the fixpoint. log gets the progression lines.
static bool simulate(const char* json, const int* side, FILE* log) {
    if (!level_from_json(&level, json)) return false;
    for (int i = 0; side && i < level.obj_count; i++) {
        if (side[i]) level.objs[i].side = side[i];
    }
    grid_init(&level);
    player_init(level.start_x, level.start_y);
    inventory_init();
    items_init();
    doors_init();
    interact_init();
    health_init();
    throw_init();
    gear_init();
    memset(touched, 0, sizeof(touched));
    peak_slots = overflow = 0;
    if (log) fprintf(log, "Progression (keys, tools and weapons in the order she reaches them):\n");
    rounds = 0;
    do {
        flood();
        rounds++;
    } while (round_step(rounds, log) && rounds < 200);
    flood();
    return true;
}

// A plain Locked Door opens from the side she can't reach without it (a
// shortcut back). If the far side then stays out of reach, the room behind
// it is a dead end with no other way in, so it opens from her side instead.
static void pick_sides(const char* json, const char* out) {
    static int side[LEVEL_MAX_OBJECTS];
    memset(side, 0, sizeof(side));
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (o->kind != OBJ_DOOR || o->lock != LOCK_OTHER_SIDE || o->side != 0) continue;
        int a = side_reached(o, -1), b = side_reached(o, 1);
        side[i] = a && !b ? 1 : b && !a ? -1 : a ? 1 : 0;
    }
    for (int pass = 0; pass < 4; pass++) {
        simulate(json, side, 0);
        bool flipped = false;
        for (int i = 0; i < level.obj_count; i++) {
            const obj_t* o = &level.objs[i];
            if (!side[i] || o->state != DOOR_LOCKED) continue;
            int a = side_reached(o, -1), b = side_reached(o, 1);
            if (a != b) {
                side[i] = a ? -1 : 1;  // dead end behind it: opens from her side
                flipped = true;
            }
        }
        if (!flipped) break;
    }
    FILE* f = fopen(out, "w");
    fprintf(f, "# Generated by reach --sides: the side each plain Locked Door opens from.\n"
               "# Do not edit; override in progression.txt.\n");
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (side[i]) fprintf(f, "set %d %d side=%d\n", o->x, o->y, side[i]);
        else if (o->kind == OBJ_DOOR && o->lock == LOCK_OTHER_SIDE && o->side == 0)
            fprintf(f, "# %d,%d: neither side reachable\n", o->x, o->y);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: reach LEVEL.json [--sides OUT.txt]\n");
        return 2;
    }
    const char* sides_out = 0;
    const char* map_out = 0;
    for (int a = 2; a + 1 < argc; a += 2) {
        if (strcmp(argv[a], "--sides") == 0) sides_out = argv[a + 1];
        if (strcmp(argv[a], "--map") == 0) map_out = argv[a + 1];
    }
    char* json = read_file(argv[1]);
    if (!json || !simulate(json, 0, 0)) {
        fprintf(stderr, "can't load %s\n", argv[1]);
        return 2;
    }
    if (sides_out) {
        pick_sides(json, sides_out);
        return 0;
    }
    simulate(json, 0, stdout);
    int total[4] = { 0 }, got[4] = { 0 };
    int cells = flood();
    for (int y = 0; y < level.rows; y++)
        for (int x = 0; x < level.cols; x++)
            if (grid_is_floor(x, y) && !obstacles_block_cell(x, y)) {
                total[floor_of(y)]++;
                got[floor_of(y)] += reach[y][x];
            }
    printf("\nFixpoint after %d rounds: %d floor cells reachable.\n", rounds, cells);
    for (int f = 1; f <= 3; f++) printf("  floor %d: %d of %d cells (%.1f%%)\n", f, got[f], total[f], 100.0 * got[f] / (total[f] ? total[f] : 1));

    // sections are bounding boxes and overlap, so a cell counts for the
    // first section whose box holds it
    printf("\nSections with floor she never reaches (cells unreached / cells):\n");
    static short owner[LEVEL_MAX_ROWS][LEVEL_MAX_COLS];
    memset(owner, -1, sizeof(owner));
    int miss[LEVEL_MAX_SECTIONS] = { 0 }, all[LEVEL_MAX_SECTIONS] = { 0 };
    for (int s = 0; s < level.section_count; s++) {
        const section_t* sec = &level.sections[s];
        for (int y = sec->y; y < sec->y + sec->h; y++)
            for (int x = sec->x; x < sec->x + sec->w; x++)
                if (grid_in_bounds(x, y) && grid_is_floor(x, y) && !obstacles_block_cell(x, y) && owner[y][x] < 0) {
                    owner[y][x] = (short)s;
                    all[s]++;
                    miss[s] += !reach[y][x];
                }
    }
    int none = 0;
    for (int s = 0; s < level.section_count; s++) {
        if (miss[s] > 0) {
            printf("  %-32s at %3d,%3d: %4d / %4d\n", level.sections[s].name, level.sections[s].x, level.sections[s].y, miss[s], all[s]);
            none++;
        }
    }
    if (!none) printf("  none\n");

    printf("\nLocked doors that never open:\n");
    int shut = 0;
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (o->kind == OBJ_DOOR && o->state == DOOR_LOCKED) {
            printf("  %-24s at %3d,%3d (F%d): %s\n", o->name, o->x, o->y, floor_of(o->y), lock_reason(o));
            shut++;
        }
    }
    if (!shut) printf("  none\n");

    printf("\nPickups she never reaches:\n");
    int lost = 0;
    for (int i = 0; i < level.obj_count; i++) {
        const obj_t* o = &level.objs[i];
        if (o->kind == OBJ_ITEM && !touched[i]) {
            printf("  %-28s at %3d,%3d (F%d)\n", item_display_name(o->item_type), o->x, o->y, floor_of(o->y));
            lost++;
        }
    }
    if (!lost) printf("  none\n");
    printf("\nSlots: at most %d of %d held at once by progression items; %d key pickups found the bag full.\n",
           peak_slots, inventory_capacity(), overflow);
    printf("\nAmmo: handgun %d, 12.7x55mm %d. Blood %d/%d. Scrap %d. Antique Coins left %d.\n", gear_ammo(0),
           gear_ammo(1), gear_blood(), gear_blood_cap(), gear_count(ITEM_SCRAP), gear_count(ITEM_COIN));
    {
        int bought = 0, cabinets = 0;
        for (int i = 0; i < level.obj_count; i++) {
            if (level.objs[i].kind == OBJ_CABINET) { cabinets++; bought += level.objs[i].state != 0; }
        }
        printf("Parlor cabinets bought: %d of %d. Requiem: %s.\n", bought, cabinets,
               inventory_has(ITEM_GUN_REQUIEM) ? "received at the goal" : "never received");
    }

    if (map_out) {
        // a picture, 4 px per cell: reachable floor green, unreachable floor
        // red, walls dark grey, locked doors that never open blue
        const int px = 4;
        FILE* f = fopen(map_out, "wb");
        fprintf(f, "P6 %d %d 255\n", level.cols * px, level.rows * px);
        static unsigned char door_cell[LEVEL_MAX_ROWS][LEVEL_MAX_COLS];
        for (int i = 0; i < level.obj_count; i++) {
            const obj_t* o = &level.objs[i];
            if (o->kind != OBJ_DOOR || o->state != DOOR_LOCKED) continue;
            for (int k = 0; k < (o->span > 0 ? o->span : 1); k++) {
                int c[4];
                door_sides(o, k, &c[0], &c[1], &c[2], &c[3]);
                if (grid_in_bounds(c[2], c[3])) door_cell[c[3]][c[2]] = 1;
            }
        }
        for (int y = 0; y < level.rows * px; y++)
            for (int x = 0; x < level.cols * px; x++) {
                int cx = x / px, cy = y / px;
                unsigned char rgb[3] = { 255, 255, 255 };
                tile_t t = level_tile(&level, cx, cy);
                if (door_cell[cy][cx]) { rgb[0] = 30; rgb[1] = 60; rgb[2] = 255; }
                else if (t == TILE_FLOOR) {
                    if (reach[cy][cx]) { rgb[0] = 120; rgb[1] = 210; rgb[2] = 120; }
                    else { rgb[0] = 235; rgb[1] = 90; rgb[2] = 90; }
                } else if (t != TILE_VOID) { rgb[0] = rgb[1] = rgb[2] = 60; }
                fwrite(rgb, 1, 3, f);
            }
        fclose(f);
    }
    return 0;
}
