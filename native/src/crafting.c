#include "crafting.h"
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
    {
        .inputs = { { ITEM_HERB, 2 } },
        .input_count = 1,
        .output = { ITEM_MIXED_HERB, 1 },
    },
};

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
        if (inventory_count_of(recipe->inputs[i].item) < recipe->inputs[i].qty) {
            return false;
        }
    }
    return true;
}

bool crafting_ready(int index) {
    return ingredients_ready(index);
}

bool craft(int index) {
    const recipe_t* recipe = 0;
    if (!recipe_at(index, &recipe) || !ingredients_ready(index)) {
        return false;
    }
    int freed = 0;
    for (int i = 0; i < recipe->input_count; i++) {
        freed += recipe->inputs[i].qty;
    }
    int holes = INV_SLOTS - inventory_count();
    if (holes + freed < recipe->output.qty) {
        return false;
    }
    int saved[INV_SLOTS];
    for (int i = 0; i < INV_SLOTS; i++) {
        saved[i] = inventory_slot(i);
    }
    for (int i = 0; i < recipe->input_count; i++) {
        for (int n = 0; n < recipe->inputs[i].qty; n++) {
            if (!inventory_remove(recipe->inputs[i].item)) {
                inventory_set(INV_SLOTS, saved);
                return false;
            }
        }
    }
    for (int n = 0; n < recipe->output.qty; n++) {
        if (!inventory_add(recipe->output.item)) {
            inventory_set(INV_SLOTS, saved);
            return false;
        }
    }
    return true;
}
