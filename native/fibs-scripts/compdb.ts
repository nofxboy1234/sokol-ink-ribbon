import { type Configurer, type Project, log, util } from 'jsr:@floooh/fibs@^1';

// Link the active config's compile_commands.json into the project root so
// clangd (and other tooling) can resolve the sokol include paths.
export function addCompdbCommand(c: Configurer) {
    c.addCommand({ name: 'compdb', help, run });
}

function help() {
    log.helpCmd(['compdb'], "link the active config's compile_commands.json into the project root");
}

function run(p: Project, _args: string[]) {
    const cfg = p.activeConfig().name;
    const src = `${p.buildDir(cfg)}/compile_commands.json`;
    const dst = `${p.dir()}/compile_commands.json`;
    if (!util.fileExists(src)) {
        log.warn(`no compile_commands.json for '${cfg}' yet - run 'fibs config ${cfg}' first`);
        return;
    }
    try {
        Deno.removeSync(dst);
    } catch (_e) {
        // nothing to remove
    }
    Deno.symlinkSync(`.fibs/build/${cfg}/compile_commands.json`, dst);
    log.info(`linked compile_commands.json -> .fibs/build/${cfg}/compile_commands.json`);
}
