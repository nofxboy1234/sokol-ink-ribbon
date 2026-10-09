import type { Builder } from 'jsr:@floooh/fibs@^1';

const sources = [
    'main.c',
    'level.c',
    'grid.c',
    'camera.c',
    'pathfind.c',
    'player.c',
    'render.c',
    'inventory.c',
    'items.c',
    'doors.c',
    'interact.c',
    'crafting.c',
    'health.c',
];

export function addMain(b: Builder) {
    b.addTarget('main', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSources(sources);
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addIncludeDirectories([t.buildDir()]);
        t.addDependencies(['sokol-static']);
        t.addJob({
            job: 'embedfiles',
            args: { outHeader: 'levels.h', files: ['level_01.json'], asText: true },
        });
    });
}

