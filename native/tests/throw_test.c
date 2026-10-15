// Native checks for the bottle throw: hit chances, Run blocking, bottle use,
// the stun and noise-led investigation. Build and run with tests/run.sh.
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

    // the bottle is spent; the last one unequips
    inventory_init();
    inventory_add(ITEM_BOTTLE);
    arm();
    fresh_enemy_at(tx + 30 < level.cols ? tx : 0, ty);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 2, "two bottles");
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 1 && throw_equipped() && throw_active(), "one spent, still equipped");
    throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r);
    CHECK(inventory_count_of(ITEM_BOTTLE) == 0 && !throw_equipped() && !throw_active(), "last bottle spent: unequipped");
    CHECK(!throw_resolve(gx, gy, tx, ty, THROW_STANCE_WAIT, &r), "no bottle, no throw");

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
    CHECK(!r.aimed && r.heard && enemy_state() == ENEMY_SEARCH && enemy_focus_x() == tx && enemy_focus_y() == ty,
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

    printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
