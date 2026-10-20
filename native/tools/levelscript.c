// levelscript: build a level JSON from script files without a window.
//   levelscript [--base level.json] [-o out.json] script.txt [more.txt...]
// Scripts run in order against one level; without -o the JSON goes to stdout.
#include "../src/level.h"
#include "../src/levelscript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char** argv) {
    static level_t lv;
    level_init(&lv);
    lv.has_tiles = 1;
    const char* out = NULL;
    int scripts = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            char* js = read_file(argv[++i]);
            if (!js || !level_from_json(&lv, js)) {
                fprintf(stderr, "levelscript: cannot load %s\n", argv[i]);
                return 1;
            }
            free(js);
            continue;
        }
        char* text = read_file(argv[i]);
        if (!text) {
            fprintf(stderr, "levelscript: cannot read %s\n", argv[i]);
            return 1;
        }
        char err[256];
        int ran = levelscript_run(&lv, text, err, sizeof(err));
        free(text);
        if (ran < 0) {
            fprintf(stderr, "%s: %s\n", argv[i], err);
            return 1;
        }
        fprintf(stderr, "%s: %d commands\n", argv[i], ran);
        scripts++;
    }
    if (scripts == 0) {
        fprintf(stderr, "usage: levelscript [--base level.json] [-o out.json] script.txt [more.txt...]\n");
        return 2;
    }
    static char json[8 * 1024 * 1024];
    int n = level_to_json(&lv, json, sizeof(json));
    if (n < 0) {
        fprintf(stderr, "levelscript: level too large to save\n");
        return 1;
    }
    FILE* f = out ? fopen(out, "wb") : stdout;
    if (!f || fwrite(json, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "levelscript: cannot write %s\n", out ? out : "stdout");
        return 1;
    }
    if (out) fclose(f);
    fprintf(stderr, "%dx%d, %d walls, %d sections, %d objects\n", lv.cols, lv.rows, lv.wall_count, lv.section_count, lv.obj_count);
    return 0;
}
