import { type Builder, type Configurer } from 'jsr:@floooh/fibs@^1';
import { addImports } from './fibs-scripts/imports.ts';
import { addLibs } from './fibs-scripts/libs.ts';
import { addConfigs } from './fibs-scripts/configs.ts';
import { addWebPageCommand } from './fibs-scripts/webpage.ts';
import { addSokolAppSamples } from './fibs-scripts/sapp.ts';
import { addD3d11Samples } from './fibs-scripts/d3d11.ts';
import { addGlfwSamples } from './fibs-scripts/glfw.ts';
import { addMetalSamples } from './fibs-scripts/metal.ts';
import { addEmscriptenSamples } from './fibs-scripts/emscripten.ts';
import { addMain } from './fibs-scripts/main.ts';
import { addMap } from './fibs-scripts/map.ts';
import { addBox3d, addBox3dImport } from './fibs-scripts/box3d.ts';

export function configure(c: Configurer) {
    addConfigs(c);
    addImports(c);
    addWebPageCommand(c);
    addBox3dImport(c);
}

export function build(b: Builder) {
    if (b.isMsvc()) {
        b.addCompileOptions([
            '/wd4324', // structure was padded due to alignment specifier
        ]);
    }
    addLibs(b);
    addBox3d(b);
    addMain(b);
    addMap(b);
    const cfg = b.activeConfig();
    if (cfg.options.sappSamples) {
        addSokolAppSamples(b);
    } else if (cfg.options.d3d11Samples) {
        addD3d11Samples(b);
    } else if (cfg.options.glfwSamples) {
        addGlfwSamples(b);
    } else if (cfg.options.metalSamples) {
        addMetalSamples(b);
    } else if (cfg.name.startsWith('emsc-')) {
        addEmscriptenSamples(b);
    }
}
