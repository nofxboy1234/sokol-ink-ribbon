import type { Builder } from 'jsr:@floooh/fibs@^1';

export function addMain(b: Builder) {
    b.addTarget('main', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSource('main.c');
        t.addSource('main.glsl');
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addIncludeDirectories([t.buildDir()]);
        t.addDependencies(['sokol-static', 'dbgui']);
        t.addCompileDefinitions({ USE_DBG_UI: '1' });
        t.addJob({
            job: 'sokolshdc',
            args: { src: 'main.glsl', out: 'main.glsl.h' },
        });
    });
}
