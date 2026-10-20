import type { Builder } from 'jsr:@floooh/fibs@^1';

const sources = [
    // Dear ImGui editor: C++, kept outside src/ (src/ is pure C)
    'editor/editor.cc',
    'src/level.c',
    'src/levelscript.c',
];

export function addEditor(b: Builder) {
    b.addTarget('editor', 'windowed-exe', (t) => {
        t.setDir('.');
        t.addSources(sources);
        t.addIncludeDirectories({ system: true, dirs: ['libs'] });
        t.addDependencies(['sokol-static', 'imgui-docking', 'stb']);
        if (b.isGcc() || b.isClang()) {
            t.addCompileOptions({ scope: 'private', opts: ['-Wno-unused-function'] });
        }
    });
    // headless: runs level scripts into a level JSON (no window, no GL)
    b.addTarget('levelscript', 'plain-exe', (t) => {
        t.setDir('.');
        t.addSources(['tools/levelscript.c', 'src/level.c', 'src/levelscript.c']);
        t.addIncludeDirectories({ system: true, dirs: ['libs'] });
    });
}
