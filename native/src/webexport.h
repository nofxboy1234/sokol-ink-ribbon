#pragma once

// Marks a function as callable from JavaScript in the Emscripten build: the
// symbol is kept alive and lands on the emscripten Module object as
// Module._<name>. Expands to nothing on native targets so the same sources
// build everywhere.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT
#endif
