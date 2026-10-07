# sokol-ink-ribbon

A map/gameplay prototype for *Grace* (Resident Evil Requiem style), split into:

- **`native/`** — the sokol + C project. It is a fork of
  [floooh/sokol-samples](https://github.com/floooh/sokol-samples) driven by
  [fibs](https://github.com/floooh/fibs), plus our own `main` target
  (`native/src/main.c`, copied from the `cube-sapp-ui` sample).
- **`web/`** — a [Void](https://void.cloud) + React + Vite app. Its index page
  loads the native `main` compiled to WebAssembly.

The native `main` is built for the host **and** for Emscripten; the wasm build is
copied into `web/public/wasm/` and rendered on the web index page.

## Layout

```
package.json                 root scripts: build:wasm, copy:wasm
scripts/copy-wasm.mjs        copies the wasm output into web/public/wasm
web/                         Void + React + Vite app (pages/index.tsx loads the wasm)
native/                      sokol-samples fork + our main target (fibs project)
  fibs                       fibs launcher (deno -> jsr:@floooh/fibs)
  fibs.ts, fibs-scripts/     build wiring (sapp samples + our main target)
  src/main.c, src/main.glsl  the main native/wasm target (cube-sapp-ui)
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

For local development, build the wasm once first (there is no `predev`):

```sh
npm run build                    # or: cd .. && npm run build:wasm && npm run copy:wasm
npm run dev                      # http://localhost:5173
```

Other web scripts: `npm test` (vitest), `npm run lint` (oxlint),
`npm run typecheck` (tsc), `npm run fmt` (oxfmt).

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
