#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT
#endif

// Probes and shortcuts only the Playwright build needs. Without TEST_HOOKS
// they become unused statics and are compiled out of the shipped wasm.
#if defined(TEST_HOOKS)
#define TEST_EXPORT WEB_EXPORT
#else
#define TEST_EXPORT static __attribute__((unused))
#endif
