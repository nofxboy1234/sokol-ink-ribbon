import type { Builder } from 'jsr:@floooh/fibs@^1';

const sources = [
    'editor.cc',
    'level.c',
];

export function addEditor(b: Builder) {
    b.addTarget('editor', 'windowed-exe', (t) => {
        t.setDir('src');
        t.addSources(sources);
        t.addIncludeDirectories({ system: true, dirs: ['../libs'] });
        t.addDependencies(['sokol-static', 'imgui']);
        if (b.isGcc() || b.isClang()) {
            t.addCompileOptions({ scope: 'private', opts: ['-Wno-unused-function'] });
        }
    });
}
