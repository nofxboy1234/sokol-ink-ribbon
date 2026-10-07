import type { Builder } from 'jsr:@floooh/fibs@^1';

const sources = [
    'main.c',
    'grid.c',
    'camera.c',
    'draw.c',
    'pathfind.c',
    'player.c',
    'render.c',
];

export function addMain(b: Builder) {
    b.addTarget('main', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSources(sources);
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addDependencies(['sokol-static', 'dbgui']);
        t.addCompileDefinitions({ USE_DBG_UI: '1' });
    });
}
