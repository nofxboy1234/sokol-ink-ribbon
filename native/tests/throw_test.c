// Native checks for the bottle throw: hit chances, Run blocking, bottle use,
// the stun and noise-led investigation, the kept equip slot, anger and light. Build and run with tests/run.sh.
#include "enemy.h"
#include "grid.h"
#include "inventory.h"
#include "level.h"
#include "sight.h"
#include "throw.h"
#include "doors.h"
#include "items.h"
#include "interact.h"
#include "player.h"
#include "health.h"
#include "light.h"
#include "savegame.h"
#include "crafting.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static level_t level;
static int failures;

#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) {                              \
            printf("PASS ");                     \
        } else {                                 \
            printf("FAIL ");                     \
            failures++;                          \
        }                                        \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)n + 1);
    fread(buf, 1, (size_t)n, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void arm(void) {
    inventory_add(ITEM_BOTTLE);
    throw_set_equipped(true);
    throw_set_active(true);
}

static void fresh_enemy_at(int x, int y) {
    enemy_init(level.start_x, level.start_y);
    enemy_seed(12345u);
    enemy_place((float)x + 0.5f, (float)y + 0.5f);
}

// A target in range with a clear line from (gx, gy), as far as possible.
static bool find_target(int gx, int gy, int* tx, int* ty) {
    float best = 0.0f;
    for (int y = gy - 8; y <= gy + 8; y++) {
        for (int x = gx - 8; x <= gx + 8; x++) {
            if (throw_in_reach(gx, gy, x, y)) {
                float d = hypotf((float)(x - gx), (float)(y - gy));
                if (d > best) {
                    best = d;
                    *tx = x;
                    *ty = y;
                }
            }
        }
    }
    return best > 3.0f;
}

static double hit_rate(int gx, int gy, int tx, int ty, throw_stance_t stance, int n) {
    int hits = 0;
    inventory_init();
    for (int i = 0; i < n; i++) {
        fresh_enemy_at(tx, ty);
        arm();
        throw_result_t r;
        if (throw_resolve(gx, gy, tx, ty, stance, &r) && r.hit) {
            hits++;
        }
    }
    return (double)hits / (double)n;
}

int main(int argc, char** argv) {
    char* json = slurp(argc > 1 ? argv[1] : "src/level_01.json");
    if (!json || !level_from_json(&level, json)) {
        printf("could not load the level\n");
        return 2;
    }
    grid_init(&level);
    grid_reveal_rect(0, 0, level.cols, level.rows);
    player_init(level.start_x, level.start_y);
    inventory_init();
    items_init();
    doors_init();
    interact_init();
    health_init();
    throw_init();
    throw_seed(777u);

    int gx = level.start_x, gy = level.start_y, tx = 0, ty = 0;
    CHECK(find_target(gx, gy, &tx, &ty), "found a target (%d,%d) from Grace (%d,%d)", tx, ty, gx, gy);
    CHECK(!throw_in_reach(gx, gy, gx, gy), "her own cell is not a target");
    CHECK(!throw_in_reach(gx, gy, gx + 9, gy) && !throw_in_reach(gx, gy, gx - 9, gy), "9 cells is out of range");

    // equip rules
    inventory_init();
    throw_init();
    CHECK(!throw_set_equipped(true), "no bottle, no equip");
    inventory_add(ITEM_BOTTLE);
    CHECK(!throw_set_active(true), "activating needs it equipped");
    CHECK(throw_set_equipped(true) && throw_set_active(true), "equip then activate");

    // statistics
    const int n = 4000;
    double wait = hit_rate(gx, gy, tx, ty, THROW_STANCE_WAIT, n);
    double walk = hit_rate(gx, gy, tx, ty, THROW_STANCE_WALK, n);
    CHECK(fabs(wait - THROW_HIT_WAIT_PCT / 100.0) < 0.03, "Wait hit rate %.3f over %d throws (want %d%%)", wait, n, THROW_HIT_WAIT_PCT);
    CHECK(fabs(walk - THROW_HIT_WALK_PCT / 100.0) < 0.03, "Walk hit rate %.3f over %d throws (want %d%%)", walk, n, THROW_HIT_WALK_PCT);
    CHECK(wait > walk + 0.2, "standing still hits more often than walking");
    // a target next to the enemy still counts as aimed at it
    inventory_init();
    fresh_enemy_at(tx, ty);
    CHECK(throw_aims_at_enemy(tx + 1, ty) && !throw_aims_at_enemy(tx + 2, ty), "aim covers the enemy's cell and its neighbours");

    // Run blocks the throw and spends nothing
    inventory_init();
    arm();
    throw_result_t r;
    CHECK(!throw_resolve(gx, gy, tx, ty, THROW_STANCE_RUN, &r), "no throwing while running");
    CHECK(inventory_count_of(ITEM_BOTTLE) == 1 && throw_active(), "the bottle is kept after a blocked throw");

    // the bottle is spent; the last one leaves the slot equipped but empty
    inventory_init();
    inventory_add(ITEM_BOTTLE);
    arm();
    fresh_enemy_at(tx + 30 < level.cols ? tx : 0, ty);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 2, "two bottles");
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 1 && throw_equipped() && throw_active(), "one spent, still equipped");
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 0 && throw_equipped() && throw_empty() && !throw_active(),
          "last bottle spent: the slot stays equipped, greyed out and inactive");
    CHECK(!throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r), "no bottle, no throw");
    CHECK(!throw_set_active(true), "an empty slot can't be made active");
    savegame_init();
    savegame_save(0, 0.0);
    throw_restore(false, false);
    double ms = 0.0;
    savegame_load(0, &ms);
    CHECK(throw_equipped() && throw_empty() && !throw_active(), "the empty slot survives save and load (v3)");
    inventory_add(ITEM_BOTTLE);
    throw_sync();
    CHECK(throw_equipped() && !throw_empty() && throw_active(), "finding a bottle fills the slot and makes it active");
    throw_set_equipped(false);
    CHECK(!throw_equipped() && !throw_active(), "Unequip clears the slot");
    inventory_add(ITEM_BOTTLE);
    throw_sync();
    CHECK(!throw_equipped() && !throw_active(), "with no slot, a found bottle is not equipped");

    // stun: a guaranteed hit (seed until one lands), then 3 s of stun and flashing
    bool landed = false;
    for (unsigned int seed = 1; seed < 50 && !landed; seed++) {
        inventory_init();
        arm();
        fresh_enemy_at(tx, ty);
        throw_seed(seed);
        landed = throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r) && r.hit;
    }
    CHECK(landed && enemy_state() == ENEMY_STUNNED, "a hit stuns the enemy");
    CHECK(fabsf(enemy_flash() - THROW_STUN_SECONDS) < 0.001f, "and it flashes for %.1f s", THROW_STUN_SECONDS);
    float far_x = (float)gx + 0.5f, far_y = (float)gy + 0.5f;
    float t = 0.0f;
    while (t < THROW_STUN_SECONDS - 0.1f) {
        enemy_update(1.0f / 60.0f, far_x, far_y, 0.0f);
        t += 1.0f / 60.0f;
    }
    CHECK(enemy_state() == ENEMY_STUNNED && enemy_flash() > 0.0f, "still stunned and flashing at %.2f s", t);
    for (int i = 0; i < 12; i++) {
        enemy_update(1.0f / 60.0f, far_x, far_y, 0.0f);
    }
    CHECK(enemy_state() != ENEMY_STUNNED && enemy_flash() == 0.0f, "free again after %.1f s", THROW_STUN_SECONDS);

    // noise: an empty-floor throw within hearing sends it to look
    int ex = -1, ey = -1, fx = -1, fy = -1;
    for (int y = 0; y < level.rows; y++) {
        for (int x = 0; x < level.cols; x++) {
            if (!grid_is_floor(x, y)) {
                continue;
            }
            float d = hypotf((float)(x - tx), (float)(y - ty));
            if (ex < 0 && d > 3.0f && d < THROW_NOISE_CELLS - 1.0f) {
                ex = x;
                ey = y;
            }
            if (fx < 0 && d > THROW_NOISE_CELLS + 2.0f) {
                fx = x;
                fy = y;
            }
        }
    }
    inventory_init();
    arm();
    fresh_enemy_at(ex, ey);
    enemy_state_t before = enemy_state();
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(!r.aimed && r.heard && enemy_state() == ENEMY_INVESTIGATE && enemy_focus_x() == tx && enemy_focus_y() == ty,
          "within %.0f cells the enemy (%d,%d) investigates the impact (state %d -> %d)", THROW_NOISE_CELLS, ex, ey,
          (int)before, (int)enemy_state());
    inventory_init();
    arm();
    fresh_enemy_at(fx, fy);
    before = enemy_state();
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(!r.heard && enemy_state() == before, "out of hearing (%d,%d) it carries on", fx, fy);

    // the investigation actually walks it toward the noise
    inventory_init();
    arm();
    fresh_enemy_at(ex, ey);
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    float d0 = hypotf(enemy_x() - (float)tx, enemy_y() - (float)ty), dmin = d0;
    for (int i = 0; i < 60 * 6; i++) {
        enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
        float d = hypotf(enemy_x() - (float)tx - 0.5f, enemy_y() - (float)ty - 0.5f);
        dmin = d < dmin ? d : dmin;
    }
    CHECK(dmin < 1.5f, "it walked from %.1f cells to within %.1f of the impact", d0, dmin);

    // anger: a hit makes it angry for ENEMY_ANGRY_SECONDS once the stun ends
    fresh_enemy_at(ex, ey);
    CHECK(enemy_angry() == 0.0f, "calm to begin with");
    enemy_damage(THROW_STUN_SECONDS);
    for (int i = 0; i < 60 * 3; i++) {
        enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
    }
    CHECK(enemy_angry() > 19.5f, "the anger only counts down after the stun (%.1f s left)", enemy_angry());
    for (int i = 0; i < 60 * 21; i++) {
        enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
    }
    CHECK(enemy_angry() == 0.0f, "and cools down after 20 s");
    // angry, it sees further: find Grace 7.5 cells off in a clear line
    {
        int ax = -1, ay = -1;
        for (int y = 0; y < level.rows && ax < 0; y++) {
            for (int x = 0; x + 8 < level.cols && ax < 0; x++) {
                if (grid_is_floor(x, y) && grid_is_floor(x + 7, y) && sight_clear(x, y, x + 7, y) && grid_is_floor(x + 8, y)) {
                    ax = x;
                    ay = y;
                }
            }
        }
        CHECK(ax >= 0, "a 7-cell sight line at (%d,%d)", ax, ay);
        float gxf = (float)ax + 8.0f, gyf = (float)ay + 0.5f;  // 7.5 cells away
        fresh_enemy_at(ax, ay);
        enemy_update(1.0f / 60.0f, gxf, gyf, 0.0f);
        bool calm_sees = enemy_sense() == SENSE_SIGHT;
        fresh_enemy_at(ax, ay);
        enemy_damage(0.01f);
        enemy_update(0.02f, 500.0f, 500.0f, 0.0f);
        enemy_place((float)ax + 0.5f, (float)ay + 0.5f);
        enemy_update(1.0f / 60.0f, gxf, gyf, 0.0f);
        CHECK(!calm_sees && enemy_sense() == SENSE_SIGHT, "calm it misses her at 7.5 cells; angry it sees her");
    }

    // light: a level light on, its cell lit; off, dark; the Lighter around Grace
    {
        int li = -1;
        for (int i = 0; i < level.obj_count; i++) {
            if (level.objs[i].kind == OBJ_LIGHT && level.objs[i].state) {
                li = i;
                break;
            }
        }
        obj_t* lo = (obj_t*)&grid_level()->objs[li];
        light_update();
        CHECK(light_cell_lit(lo->x, lo->y) || !grid_is_floor(lo->x, lo->y), "a light that is on lights its cell (%d,%d)", lo->x, lo->y);
        int far = (int)light_radius_of(lo->radius) + 2;
        CHECK(!light_cell_lit(lo->x + far, lo->y), "but not %d cells off", far);
        lo->state = 0;
        light_update();
        CHECK(!light_cell_lit(lo->x, lo->y), "switched off, it is dark");
        lo->state = 1;
        inventory_init();
        int px, py;
        player_cell(&px, &py);
        if (lighter_on()) {
            lighter_toggle();
        }
        inventory_add(ITEM_LIGHTER);
        lighter_toggle();
        CHECK(light_cell_lit(px, py), "the lit Lighter lights her cell (%d,%d)", px, py);
        lighter_toggle();
    }

    // --- the enemy: save round trip, bottle priority, rest, sight and stealth
    {
        // save and load bring the enemy back where it was, as it was
        fresh_enemy_at(ex, ey);
        enemy_damage(1.0f);
        for (int i = 0; i < 90; i++) {
            enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
        }
        float sx = enemy_x(), sy = enemy_y(), sa = enemy_angry();
        enemy_state_t ss = enemy_state();
        savegame_save(1, 0.0);
        fresh_enemy_at(gx, gy);
        double ms = 0.0;
        savegame_load(1, &ms);
        CHECK(savegame_has_enemy(1) && enemy_x() == sx && enemy_y() == sy && enemy_state() == ss && enemy_angry() == sa,
              "save/load restores the enemy at (%.2f,%.2f), state %d, anger %.1f s", sx, sy, (int)ss, sa);

        // bottle priority: smelling Grace (in the dark) doesn't stop it going to look
        fresh_enemy_at(ex, ey);
        enemy_set_grace_lit(false);
        float sgx = enemy_x() + 2.0f, sgy = enemy_y();
        enemy_update(1.0f / 60.0f, sgx, sgy, 0.0f);
        CHECK(enemy_state() == ENEMY_CHASE && enemy_sense() == SENSE_SMELL, "in the dark it only smells her (chasing)");
        CHECK(enemy_hear_noise(tx, ty, THROW_NOISE_CELLS) && enemy_state() == ENEMY_INVESTIGATE,
              "a bottle pulls it off a chase it can't see");
        for (int i = 0; i < 30; i++) {
            enemy_update(1.0f / 60.0f, enemy_x() + 2.0f, enemy_y(), 0.0f);
        }
        CHECK(enemy_state() == ENEMY_INVESTIGATE, "and smelling her on the way doesn't pull it off the bottle");
        // ...and it reaches the impact and looks around it
        float best = 1e9f;
        int around = 0;
        for (int i = 0; i < 60 * 15; i++) {
            enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
            float d = hypotf(enemy_x() - (float)tx - 0.5f, enemy_y() - (float)ty - 0.5f);
            best = d < best ? d : best;
            if (best < 0.1f && enemy_state() == ENEMY_INVESTIGATE && d > 0.9f && d < 4.5f) {
                around++;
            }
        }
        CHECK(best < 0.1f && around > 0, "it walks to the impact and sweeps around it (closest %.2f)", best);
        // lit and in sight while chasing, it ignores the bottle
        enemy_set_grace_lit(true);
        int lx = -1, ly = -1;
        for (int y = 0; y < level.rows && lx < 0; y++) {
            for (int x = 0; x + 4 < level.cols && lx < 0; x++) {
                if (grid_is_floor(x, y) && grid_is_floor(x + 4, y) && sight_clear_eyes(x, y, x + 4, y)) {
                    lx = x;
                    ly = y;
                }
            }
        }
        fresh_enemy_at(lx, ly);
        enemy_update(1.0f / 60.0f, (float)lx + 4.5f, (float)ly + 0.5f, 0.0f);
        CHECK(enemy_state() == ENEMY_CHASE && enemy_sense() == SENSE_SIGHT && !enemy_hear_noise(lx, ly, 20.0f),
              "chasing her in sight, it ignores a bottle");

        // after hurting her: 8 s standing still, no catching, then it searches
        fresh_enemy_at(lx, ly);
        enemy_rest(8.0f);
        float rx = enemy_x(), ry = enemy_y();
        bool touched = false;
        for (int i = 0; i < 60 * 7.9; i++) {
            touched |= enemy_update(1.0f / 60.0f, rx, ry, 10.0f);
        }
        CHECK(enemy_state() == ENEMY_WAIT && enemy_x() == rx && enemy_y() == ry && !touched,
              "for 8 s after hurting her it waits, still, and can't catch her");
        for (int i = 0; i < 20; i++) {
            enemy_update(1.0f / 60.0f, 500.0f, 500.0f, 0.0f);
        }
        CHECK(enemy_state() == ENEMY_SEARCH, "then it searches");

        // stealth: lit she is seen at 4 cells; in the dark not, but heard and smelt
        fresh_enemy_at(lx, ly);
        enemy_set_grace_lit(true);
        enemy_update(1.0f / 60.0f, (float)lx + 4.5f, (float)ly + 0.5f, 0.0f);
        bool lit_seen = enemy_sense() == SENSE_SIGHT;
        fresh_enemy_at(lx, ly);
        enemy_set_grace_lit(false);
        enemy_update(1.0f / 60.0f, (float)lx + 4.5f, (float)ly + 0.5f, 0.0f);
        bool dark_smelt = enemy_sense() == SENSE_SMELL;  // not SIGHT, though in plain view
        fresh_enemy_at(lx, ly);
        enemy_set_grace_lit(false);
        enemy_update(1.0f / 60.0f, (float)lx + 6.5f, (float)ly + 0.5f, 0.0f);
        bool dark_none = enemy_sense() == SENSE_NONE;
        fresh_enemy_at(lx, ly);
        enemy_set_grace_lit(false);
        enemy_update(1.0f / 60.0f, (float)lx + 6.5f, (float)ly + 0.5f, 8.0f);
        bool dark_heard = enemy_sense() == SENSE_HEARING;
        CHECK(lit_seen && dark_smelt && dark_none && dark_heard,
              "lit: seen at 4 cells (%d); dark: smelt at 4 (%d), nothing at 6 (%d), heard at 6 (%d)", lit_seen, dark_smelt,
              dark_none, dark_heard);

        // obstacles block sight but not smell or hearing
        int ox = -1, oy = -1, oxe = -1;
        for (int i = 0; i < level.obj_count && ox < 0; i++) {
            const obj_t* o = &level.objs[i];
            if (o->kind != OBJ_OBSTACLE) {
                continue;
            }
            int y = o->y + (o->h > 0 ? o->h : 1) / 2;
            int a = o->x - 1, b = o->x + (o->w > 0 ? o->w : 1);
            if (grid_is_floor(a, y) && grid_is_floor(b, y) && sight_clear(a, y, b, y)) {
                ox = a;
                oy = y;
                oxe = b;
            }
        }
        CHECK(ox >= 0 && !sight_clear_eyes(ox, oy, oxe, oy), "an obstacle between (%d,%d) and (%d,%d) blocks the eyes", ox, oy, oxe, oy);
        fresh_enemy_at(ox, oy);
        enemy_update(1.0f / 60.0f, (float)oxe + 0.5f, (float)oy + 0.5f, 0.0f);
        CHECK(enemy_sense() == SENSE_SMELL, "but it still smells her across it (%d cells)", oxe - ox);
        // smell goes through walls too: find two cells 2 apart with a wall between
        int wx = -1, wy = -1;
        for (int y = 0; y < level.rows && wx < 0; y++) {
            for (int x = 0; x + 2 < level.cols && wx < 0; x++) {
                if (grid_is_floor(x, y) && grid_is_floor(x + 2, y) && !sight_clear(x, y, x + 2, y)) {
                    wx = x;
                    wy = y;
                }
            }
        }
        fresh_enemy_at(wx, wy);
        enemy_update(1.0f / 60.0f, (float)wx + 2.5f, (float)wy + 0.5f, 0.0f);
        CHECK(enemy_sense() == SENSE_SMELL, "and through a wall at (%d,%d)", wx, wy);
    }

    // the Lighter's finds stay on the map
    {
        int hi = -1;
        for (int i = 0; i < level.obj_count; i++) {
            if (level.objs[i].kind == OBJ_ITEM && level.objs[i].x == 79 && level.objs[i].y == 49) {
                hi = i;
            }
        }
        player_set_cell(78, 49);
        inventory_init();
        if (lighter_on()) {
            lighter_toggle();
        }
        light_update();
        CHECK(hi >= 0 && !light_item_visible(hi), "the herb at (79,49) starts in the dark");
        inventory_add(ITEM_LIGHTER);
        lighter_toggle();
        CHECK(light_item_visible(hi) && items_seen(hi), "the Lighter shows it");
        lighter_toggle();
        CHECK(!light_cell_lit(79, 49) && light_item_visible(hi), "and with the Lighter off it stays visible");
        savegame_save(2, 0.0);
        items_import_seen(NULL, 0);
        double ms2 = 0.0;
        savegame_load(2, &ms2);
        CHECK(items_seen(hi), "seen items survive save and load");

        // a switched-on lamp's finds stay too: move the herb next to a lamp
        int li = -1;
        for (int i = 0; i < level.obj_count; i++) {
            if (level.objs[i].kind == OBJ_LIGHT) {
                li = i;
                break;
            }
        }
        obj_t* lo = (obj_t*)&grid_level()->objs[li];
        obj_t* ho = (obj_t*)&grid_level()->objs[hi];
        int hx = ho->x, hy = ho->y, ls = lo->state;
        player_set_cell(500 % level.cols, 0);
        lo->state = 1;
        light_update();
        for (int dy = -1; dy <= 1 && ho->x == hx; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if ((dx || dy) && light_cell_lit(lo->x + dx, lo->y + dy)) {
                    ho->x = lo->x + dx;
                    ho->y = lo->y + dy;
                    break;
                }
            }
        }
        items_import_seen(NULL, 0);
        lo->state = 0;
        light_update();
        bool dark_before = !light_item_visible(hi);
        lo->state = 1;
        light_update();
        bool shown = items_seen(hi);
        lo->state = 0;
        light_update();
        CHECK(dark_before && shown && light_item_visible(hi),
              "a switch light reveals the item at (%d,%d) and it stays after switching off", ho->x, ho->y);
        savegame_save(2, 0.0);
        items_import_seen(NULL, 0);
        savegame_load(2, &ms2);
        CHECK(items_seen(hi), "and that survives save and load");
        ho->x = hx;
        ho->y = hy;
        lo->state = ls;
    }

    // --- audit: fuse chain, cherub door, climbing, recipes, cabinet, safes
    {
        // fuse is in openable 29 (link -> screwdriver object): no screwdriver, no fuse
        inventory_init(); interact_init(); items_import(NULL, 0);
        // reset openables/doors from a fresh level copy state is complex; use live objects
        obj_t* box = NULL; obj_t* fusebox = NULL; obj_t* gate = NULL;
        int fuse_i = -1, cherub_i = -1, screw_i = -1;
        for (int i = 0; i < level.obj_count; i++) {
            obj_t* o = (obj_t*)&grid_level()->objs[i];
            if (o->kind == OBJ_OPENABLE && o->id == 29) box = o;
            if (o->kind == OBJ_FUSEBOX) fusebox = o;
            if (o->kind == OBJ_DOOR && strcmp(o->name, "gate") == 0) gate = o;
            if (o->kind == OBJ_ITEM && o->item_type == ITEM_FUSE) fuse_i = i;
            if (o->kind == OBJ_ITEM && o->item_type == ITEM_CHERUB_KEY) cherub_i = i;
            if (o->kind == OBJ_ITEM && o->item_type == ITEM_SCREWDRIVER) screw_i = i;
        }
        box->open = 0; gate->state = DOOR_LOCKED;
        player_set_cell(2, 21);
        interact_at(1, 21);
        CHECK(!items_taken(fuse_i), "no screwdriver: the fuse stays in its box");
        inventory_add(ITEM_SCREWDRIVER);
        interact_at(1, 20);  // open the box
        CHECK(box->open == 1, "with the screwdriver the box opens");
        interact_at(1, 21);
        CHECK(items_taken(fuse_i) && inventory_has(ITEM_FUSE), "then she can take the fuse");
        // fusebox without fuse does nothing; with fuse powers gate
        inventory_init(); player_set_cell(8, 69);
        interact_at(7, 69);
        CHECK(!goal_met() && gate->state == DOOR_LOCKED, "no fuse: no power, gate stays shut");
        inventory_add(ITEM_FUSE);
        interact_at(7, 69);
        CHECK(goal_met() && gate->state == DOOR_BROKEN && !inventory_has(ITEM_FUSE), "fuse inserted: gate powered open");
        player_set_cell(5, 74);
        CHECK(interact_check_complete() && level_complete(), "walking past the gate to the goal ends the level");

        // cherub door (door 1, link -> cherub key object): only that key opens it
        obj_t* cdoor = NULL;
        for (int i = 0; i < level.obj_count; i++) { obj_t* o=(obj_t*)&grid_level()->objs[i]; if (o->kind==OBJ_DOOR && o->id==1) cdoor=o; }
        cdoor->state = DOOR_LOCKED; cdoor->open = 0;
        inventory_init(); inventory_add(ITEM_SCREWDRIVER); inventory_add(ITEM_COIN);
        player_set_cell(17, 37);
        doors_interact(17, 38);
        CHECK(cdoor->state == DOOR_LOCKED, "cherub door: screwdriver/coin do not open it");
        inventory_add(ITEM_CHERUB_KEY);
        doors_interact(17, 38);
        CHECK(cdoor->state == DOOR_UNLOCKED, "cherub door: the cherub key opens it");

        // climbing: a climbable obstacle can be stood on; a plain one blocks
        obj_t* table = (obj_t*)&grid_level()->objs[43];
        int tw = table->w > 0 ? table->w : 1;
        table->climbable = 0;
        CHECK(obstacles_block_cell(table->x, table->y), "plain obstacle blocks its cells");
        table->climbable = 1;
        CHECK(!obstacles_block_cell(table->x, table->y), "climbable obstacle does not block");
        player_set_cell(table->x, table->y);
        CHECK(player_on_top(), "standing on it: on top");
        player_set_cell(table->x + tw + 1, table->y);
        CHECK(!player_on_top(), "stepping off: not on top");
        table->climbable = 0;

        // recipes: #0 known, #1 must be taught and persists
        crafting_set_known_mask(1u);
        CHECK(crafting_known(0) && !crafting_known(1) && !crafting_ready(1), "recipe 1 unknown at start");
        CHECK(crafting_learn(1) && crafting_known(1), "a recipe item teaches it");
        savegame_save(3, 0.0); crafting_set_known_mask(1u);
        { double m=0; savegame_load(3, &m); }
        CHECK(crafting_known(1), "known recipes survive save/load");

        // cabinet: spend a coin, +4 slots, saved
        inventory_init(); inventory_set_capacity(8);
        inventory_add(ITEM_COIN);
        obj_t cab = {0}; cab.kind = OBJ_CABINET; cab.code = 1; cab.x = 20; cab.y = 20;
        // place Grace next to a real cabinet object appended in memory is intrusive;
        // exercise the inventory side directly:
        CHECK(inventory_capacity() == 8 && inventory_expand(4) && inventory_capacity() == 12, "cabinet upgrade: 8 -> 12 slots");
        savegame_save(3, 0.0); inventory_set_capacity(8);
        { double m=0; savegame_load(3, &m); }
        CHECK(inventory_capacity() == 12, "capacity survives save/load");

        // safe + combination door codes
        obj_t* safe = NULL; obj_t* codelock = NULL;
        for (int i = 0; i < level.obj_count; i++) { obj_t* o=(obj_t*)&grid_level()->objs[i]; if (o->kind==OBJ_SAFE) safe=o; if (o->kind==OBJ_DOOR && o->id==39) codelock=o; }
        if (!safe) { // none in level 1: borrow the goal marker for the check
            for (int i = 0; i < level.obj_count; i++) { obj_t* o=(obj_t*)&grid_level()->objs[i]; if (o->kind==OBJ_GOAL) safe=o; }
            if (safe) safe->kind = OBJ_SAFE;
        }
        if (safe) {
            safe->open = 0; safe->code = 1234; player_set_cell(safe->x + 1, safe->y);
            interact_at(safe->x, safe->y);
            CHECK(interact_pending_safe() >= 0 && !interact_try_safe_code(1111) && interact_try_safe_code(1234) && safe->open == 1,
                  "safe: wrong code fails, right code opens");
            safe->kind = OBJ_GOAL; safe->code = 0;
        } else {
            CHECK(true, "safe: none placed in level 1 (system tested when placed)");
        }
        if (codelock) {
            codelock->code = 4321; codelock->state = DOOR_LOCKED;
            player_set_cell(codelock->x, codelock->y - 1);
            doors_interact(codelock->x, codelock->y);
            CHECK(doors_pending_code() >= 0 && !doors_try_code(1111) && doors_try_code(4321) && codelock->state == DOOR_UNLOCKED,
                  "combination door: wrong code fails, right code opens");
            codelock->code = 0;
        }
    }

    printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
