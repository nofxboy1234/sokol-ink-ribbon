# sokol-ink-ribbon

sokol-ink-ribbon is the *Grace* map app from the design plan in
`ref/care-center-01-plan`: a turn-based, grid map rendered with sokol + C and
embedded in a React app, plus a dear-imgui level editor. It is split into:

- **`native/`** — the sokol + C project. It is a fork of
  [floooh/sokol-samples](https://github.com/floooh/sokol-samples) driven by
  [fibs](https://github.com/floooh/fibs), plus two of our own targets:
  - `main` (`native/src/main.c`) — the map runtime: grid, smooth follow camera,
    A* movement, section reveal, doors, items, inventory and health. It builds
    for the host **and** for Emscripten (the wasm canvas).
  - `editor` (`native/src/editor.cc`) — a desktop-only dear-imgui level editor:
    a snapping wall line tool, an object palette, property/link editing and
    JSON save/load. It shares the `level` module with the runtime.
- **`web/`** — a [Void](https://void.cloud) + React + Vite app. Its index page
  loads the native `main` wasm, and a `records` page shows saved runs and the
  leaderboard (Cloudflare D1 via Void, Better Auth email/password).

The runtime reads the level from JSON that the editor writes and fibs embeds
into the wasm at build time (`native/src/level_01.json` → `native/src/levels.h`).

## Layout

```
package.json                 root scripts: build:wasm, copy:wasm
scripts/copy-wasm.mjs        copies the wasm output into web/public/wasm
web/                         Void + React + Vite app
  pages/index.tsx            map canvas + ITEMS/CRAFTING/FILES panes
  pages/records.tsx          saved runs and the leaderboard
  src/                       React components + the wasm bridge
  db/schema.ts, migrations/  D1 (Drizzle) schema: runs + Better Auth tables
  routes/                    /runs and /leaderboard API endpoints
  tests/                     Playwright functional tests (production build)
native/                      sokol-samples fork + our main/editor targets
  fibs                       fibs launcher (deno -> jsr:@floooh/fibs)
  fibs.ts, fibs-scripts/     build wiring (sapp samples + main + editor)
  src/                       map runtime, level model and the imgui editor
  libs/json/jsmn.h           vendored JSON parser for the embedded level
  sapp/ libs/ html5/ ...     upstream sokol-samples sources and assets
  scripts/fibs-completion.bash
```

## Prerequisites

- [deno](https://docs.deno.com/runtime/getting_started/installation/) (runs fibs)
- [cmake](https://cmake.org/) and a C/C++ toolchain (GCC/Clang/MSVC)
- [ninja](https://ninja-build.org/) (recommended)
- [Node.js](https://nodejs.org/) 24+ and npm (for the web app)
- On Linux, the usual dev packages for OpenGL/X11/ALSA/Vulkan, e.g.
  `libgl1-mesa-dev libegl1-mesa-dev mesa-common-dev xorg-dev libasound-dev libvulkan-dev`

Emscripten is **not** required up front; fibs installs it on demand (see below).

## Fresh clone

```sh
git clone <this-repo> sokol-ink-ribbon
cd sokol-ink-ribbon
```

fibs fetches its dependencies (`sokol`, `box3d`, `dcimgui`, `fibs-extras`,
`fibs-libs`, …) into `native/.fibs/imports/` on the first build. The Emscripten
SDK lives in `native/.fibs/sdks/emsdk` and is installed once:

```sh
cd native
./fibs emsdk install      # one-time, large download (only needed for the wasm build)
```

Install the web dependencies:

```sh
cd ../web
npm ci
```

Build the wasm and the web app (the `prebuild` script rebuilds the wasm and
copies it into `web/public/wasm`):

```sh
npm run build
```

For local development, build the wasm first (there is no `predev`):

```sh
npm run build                    # full web build (rebuilds the wasm too)
npm run dev                      # http://localhost:5173
```

The dev server does not rebuild the wasm, so after changing native code refresh it
from the repo root and hard-reload the tab (Ctrl+Shift+R) to bypass the browser's
wasm cache:

```sh
npm run wasm                     # fibs build main (emscripten) + copy into web/public/wasm
```

Other web scripts: `npm run lint` (oxlint), `npm run typecheck` (tsc),
`npm run fmt` (oxfmt).

## Functional tests

The tests are Playwright specs that run against the **production** web build
(which exercises the native wasm inside it). Install the browser once, then run
them:

```sh
cd web
npm run test:install   # one-time: downloads Chromium
npm test               # builds, serves, and runs tests/smoke.spec.ts
```

Auth is enabled, so a local production preview needs a Better Auth secret. Put
one in `web/.env` (`BETTER_AUTH_SECRET=...`); `void deploy` stores it as a
Worker secret instead.

## Building the native project

All fibs commands are run from `native/`.

List build configs and pick one for your platform, e.g. on Linux:

```sh
./fibs list configs
./fibs config sapp-gl-linux-ninja-release    # OpenGL, ninja, release
```

Other platforms have equivalents such as `sapp-metal-macos-ninja-release`
(macOS) and `sapp-d3d11-win-msvc-release` (Windows). The selected config is
remembered in `native/.fibs/settings.json`; `./fibs config --get` shows it.

Build and run the **main** target natively:

```sh
./fibs build main
./fibs run main
```

## Level editor

The editor is desktop-only (not part of the wasm build). With a desktop config
selected (`sapp-gl-linux-ninja-release` on Linux):

```sh
./fibs build editor
./fibs run editor
```

It reads and writes `native/src/level_01.json` (relative to `native/`). Use the
wall line tool to draw walls, the place tool to add doors/items/lights/etc., and
the properties panel to edit the selected object. Saving rewrites the level JSON;
the runtime picks it up on the next `npm run wasm` (the file is embedded as
`native/src/levels.h`).

Build the **main** target for the browser (WebAssembly):

```sh
./fibs config sapp-gles-emsc-ninja-release
./fibs build main
```

The output is `native/.fibs/dist/sapp-gles-emsc-ninja-release/main.{js,wasm,html}`.
Copy it into the web app with the root script:

```sh
cd ..            # repo root
npm run copy:wasm
```

(The web app's `prebuild` runs `build:wasm` and `copy:wasm` for you.)

## Building and running the sokol samples

With a sokol-samples config selected (`sapp-gl-linux-ninja-release` on Linux,
`./fibs config sapp-gl-linux-ninja-release`), build everything:

```sh
cd native
./fibs build
```

List runnable targets and run any sample:

```sh
./fibs list targets --exe
./fibs run cube-sapp-ui
./fibs run triangle-sapp
./fibs run box3d-simple-sapp
```

Every sample also has a `-ui` variant (debug UI). Use `./fibs build <target>`
to build a single target, and `./fibs run <target>` to run it.

## Editor support (clangd / go-to-definition)

The build exports a `compile_commands.json` (via `CMAKE_EXPORT_COMPILE_COMMANDS`)
so clangd can resolve the sokol headers. Link it into the project root for the
active config:

```sh
cd native
./fibs compdb
```

This creates `native/compile_commands.json` as a symlink into
`native/.fibs/build/<config>/`. Re-run `./fibs compdb` after switching build
configs. clangd then finds it automatically for any file under `native/`, so
go-to-definition works on `sokol_gfx.h`, `sokol_app.h`, `sokol_gl.h`, etc.

## fibs tab-completion

Source the completion script (bash):

```sh
. native/scripts/fibs-completion.bash
```

Add that line to your shell rc to keep it. It completes fibs subcommands, build
configs, targets (for `build`/`run`/`clean`) and imports (for `link`/`unlink`),
for both `./fibs` and a system-wide `fibs`.

## Updating dependencies

sokol and box3d are managed by fibs and can be updated in place:

```sh
cd native
./fibs update sokol
./fibs update box3d
```

`native/` itself is a fork of sokol-samples (it carries our `main` target and a
small `box3d` compatibility shim in `fibs-scripts/box3d.ts`), so upstream
sokol-samples changes are merged in manually rather than pulled.
