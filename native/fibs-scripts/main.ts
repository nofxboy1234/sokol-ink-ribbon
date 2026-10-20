import type { Builder } from 'jsr:@floooh/fibs@^1';

const sources = [
    'main.c',
    'level.c',
    'grid.c',
    'camera.c',
    'pathfind.c',
    'player.c',
    'enemy.c',
    'render.c',
    'stream.c',
    'inventory.c',
    'items.c',
    'doors.c',
    'interact.c',
    'crafting.c',
    'health.c',
    'savegame.c',
    'sight.c',
    'throw.c',
    'light.c',
    'grace_state.c',
];

export function addMain(b: Builder) {
    // Dear ImGui is C++, so the runtime's one ImGui call is a tiny C++ library
    // outside src/ (src/ stays pure C).
    b.addTarget('imgui-glue', 'lib', (t) => {
        t.setDir('imgui-glue');
        t.addSources(['tooltip.cc']);
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addDependencies(['imgui']);
    });
    b.addTarget('main', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSources(sources);
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addIncludeDirectories([t.buildDir()]);
        t.addDependencies(['sokol-static', 'dbgui', 'imgui-glue']);
        t.addCompileDefinitions({ USE_DBG_UI: '1' });
        // TEST_HOOKS=1 at config time keeps the test-only exports (Playwright build)
        if (Deno.env.get('TEST_HOOKS') === '1') {
            t.addCompileDefinitions({ TEST_HOOKS: '1' });
        }
        if (b.activeConfig().platform === 'emscripten') {
            t.addLinkOptions(['-sEXPORTED_RUNTIME_METHODS=UTF8ToString']);
        }
        t.addJob({
            job: 'embedfiles',
            args: { outHeader: 'levels.h', files: ['level_01.json'], asText: true },
        });
    });
}

