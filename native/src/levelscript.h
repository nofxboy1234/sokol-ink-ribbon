#pragma once
#include "level.h"

#ifdef __cplusplus
extern "C" {
#endif

// Run a level script (one command per line, '#' starts a comment) against
// lv. Returns the number of commands run, or -1 on the first bad line with a
// "line N: message" in err. The tile map is recompiled into walls at the end.
int levelscript_run(level_t* lv, const char* text, char* err, int errcap);

#ifdef __cplusplus
}
#endif
