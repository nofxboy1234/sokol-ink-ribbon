#include "crafting.h"
#include "gear.h"
#include "inventory.h"
#include "level.h"

#define RECIPE_INPUT_MAX 4

typedef struct {
    int item;
    int qty;
} recipe_part_t;

typedef struct {
    recipe_part_t inputs[RECIPE_INPUT_MAX];
    int input_count;
    recipe_part_t output;
} recipe_t;

static const recipe_t recipes[] = {
    { .inputs = { { ITEM_HERB, 2 } }, .input_count = 1, .output = { ITEM_MIXED_HERB, 1 } },
    // taught by a recipe item in a later level; none is placed in level 1
    { .inputs = { { ITEM_HERB, 3 } }, .input_count = 1, .output = { ITEM_INJECTOR, 1 } },
    // taught by Blood Specimens at a Laser Microscope (gear_analyse_specimen)
    { .inputs = { { ITEM_SCRAP, 1 }, { ITEM_INFECTED_BLOOD, 60 } }, .input_count = 2, .output = { ITEM_HEMOLYTIC_INJECTOR, 1 } },
    { .inputs = { { ITEM_SCRAP, 1 }, { ITEM_INFECTED_BLOOD, 20 } }, .input_count = 2, .output = { ITEM_HANDGUN_AMMO, 6 } },
    { .inputs = { { ITEM_INFECTED_BLOOD, 40 } }, .input_count = 1, .output = { ITEM_INK_RIBBON, 1 } },
    { .inputs = { { ITEM_INJECTOR, 1 }, { ITEM_INFECTED_BLOOD, 130 } }, .input_count = 2, .output = { ITEM_STEROIDS, 1 } },
    { .inputs = { { ITEM_INJECTOR, 1 }, { ITEM_INFECTED_BLOOD, 130 } }, .input_count = 2, .output = { ITEM_STABILIZER, 1 } },
};
// bit i set = recipe i known; recipe 0 is known from the start
static unsigned int known_mask = 1u;

static int recipe_len(void) {
    return (int)(sizeof(recipes) / sizeof(recipes[0]));
}

static bool part_ok(recipe_part_t part) {
    return part.item >= 0 && part.item < ITEM_COUNT && part.qty >= 1;
}

static bool recipe_ok(const recipe_t* recipe) {
    if (recipe->input_count < 1 || recipe->input_count > RECIPE_INPUT_MAX) {
        return false;
    }
    if (!part_ok(recipe->output)) {
        return false;
    }
    for (int i = 0; i < recipe->input_count; i++) {
        if (!part_ok(recipe->inputs[i]) || recipe->inputs[i].item == recipe->output.item) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (recipe->inputs[j].item == recipe->inputs[i].item) {
                return false;
            }
        }
    }
    return true;
}

static bool recipe_at(int index, const recipe_t** out) {
    if (index < 0 || index >= recipe_len() || !recipe_ok(&recipes[index])) {
        return false;
    }
    *out = &recipes[index];
    return true;
}

int crafting_count(void) {
    return recipe_len();
}

static recipe_part_t part_of(int index, int part) {
    recipe_part_t absent = { -1, 0 };
    const recipe_t* recipe = 0;
    if (!recipe_at(index, &recipe)) {
        return absent;
    }
    if (part < 0) {
        return recipe->output;
    }
    if (part >= recipe->input_count) {
        return absent;
    }
    return recipe->inputs[part];
}

int crafting_part_item(int index, int part) {
    return part_of(index, part).item;
}

int crafting_part_qty(int index, int part) {
    return part_of(index, part).qty;
}

static bool ingredients_ready(int index) {
    const recipe_t* recipe = 0;
    if (!recipe_at(index, &recipe)) {
        return false;
    }
    for (int i = 0; i < recipe->input_count; i++) {
        if (gear_count(recipe->inputs[i].item) < recipe->inputs[i].qty) {
            return false;
        }
    }
    return true;
}

bool crafting_known(int index) { return index >= 0 && index < recipe_len() && (known_mask & (1u << index)) != 0; }
bool crafting_learn(int index) {
    if (index < 0 || index >= recipe_len() || crafting_known(index)) return false;
    known_mask |= (1u << index); return true;
}
unsigned int crafting_known_mask(void) { return known_mask; }
void crafting_set_known_mask(unsigned int m) { known_mask = m | 1u; }
bool crafting_ready(int index) {
    return crafting_known(index) && ingredients_ready(index);
}

static int slots_for(recipe_part_t part) {
    return gear_counted(part.item) ? 0 : part.qty;
}

bool craft(int index) {
    const recipe_t* recipe = 0;
    if (!recipe_at(index, &recipe) || !crafting_ready(index)) {
        return false;
    }
    int freed = 0;
    for (int i = 0; i < recipe->input_count; i++) {
        freed += slots_for(recipe->inputs[i]);
    }
    if (inventory_capacity() - inventory_count() + freed < slots_for(recipe->output)) {
        return false;
    }
    for (int i = 0; i < recipe->input_count; i++) {
        gear_spend(recipe->inputs[i].item, recipe->inputs[i].qty);
    }
    return gear_give(recipe->output.item, recipe->output.qty);
}
