import type { Builder } from 'jsr:@floooh/fibs@^1';

export function addMap(b: Builder) {
    b.addTarget('map', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSource('map.c');
        t.addDependencies(['sokol-static']);
    });
}
