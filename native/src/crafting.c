#include "crafting.h"
#include "inventory.h"
#include "level.h"

typedef struct {
    int a, b, out;
} recipe_t;

static const recipe_t recipes[] = {
    { ITEM_HERB, ITEM_HERB, ITEM_HERB },
    { ITEM_HERB, ITEM_BOTTLE, ITEM_HERB },
    { ITEM_FUSE, ITEM_SCREWDRIVER, ITEM_FUSE },
};

int crafting_count(void) {
    return (int)(sizeof(recipes) / sizeof(recipes[0]));
}

bool craft(int index) {
    if (index < 0 || index >= crafting_count()) {
        return false;
    }
    const recipe_t* r = &recipes[index];
    if (!inventory_has(r->a) || !inventory_has(r->b)) {
        return false;
    }
    inventory_remove(r->a);
    inventory_remove(r->b);
    inventory_add(r->out);
    return true;
}
