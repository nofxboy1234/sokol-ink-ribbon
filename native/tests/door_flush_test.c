// Every door in the level sits flush in its doorway: both ends touch a jamb
// and it lies on the wall band's line, exactly where the editor's door tool
// (level_door_fit) would place it.
#include "level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static level_t lv;
static char buf[1 << 21];

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "src/level_01.json";
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    level_init(&lv);
    if (!level_from_json(&lv, buf)) {
        printf("cannot parse %s\n", path);
        return 1;
    }
    int doors = 0, bad = 0, normal_wrong = 0;
    for (int i = 0; i < lv.obj_count; i++) {
        const obj_t* d = &lv.objs[i];
        if (d->kind != OBJ_DOOR) continue;
        doors++;
        if (!level_door_flush(&lv, d)) {
            bad++;
            printf("  not flush: door #%d '%s' at %d,%d %s span %d\n", d->id, d->name, d->x, d->y,
                   d->horizontal ? "h" : "v", d->span);
        }
        if (strcmp(d->name, "door") == 0 && d->span != DOOR_SPAN) {
            normal_wrong++;
            printf("  normal door #%d is span %d, not %d\n", d->id, d->span, DOOR_SPAN);
        }
    }
    printf("%d doors, %d not flush, %d normal doors off span\n", doors, bad, normal_wrong);
    printf(doors > 0 && !bad && !normal_wrong ? "ALL PASS\n" : "FAILED\n");
    return doors > 0 && !bad && !normal_wrong ? 0 : 1;
}
