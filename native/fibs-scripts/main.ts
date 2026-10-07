import type { Builder } from 'jsr:@floooh/fibs@^1';

export function addMain(b: Builder) {
    b.addTarget('main', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSource('main.c');
        t.addDependencies(['sokol-static']);
    });
}
