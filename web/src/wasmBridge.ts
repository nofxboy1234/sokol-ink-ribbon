export type EmscriptenModule = {
  canvas?: HTMLCanvasElement;
  print?: (...args: unknown[]) => void;
  printErr?: (...args: unknown[]) => void;
  _ui_revision?: () => number;
  _grid_width?: () => number;
  _grid_height?: () => number;
  _player_cell_x?: () => number;
  _player_cell_y?: () => number;
  _player_is_walking?: () => number;
  _player_total_steps?: () => number;
  _level_revealed?: () => number;
  _level_cell_px?: () => number;
  _web_inventory_count?: () => number;
  _web_inventory_slot?: (index: number) => number;
  _web_inventory_has?: (item: number) => number;
  _health_value?: () => number;
  _items_collected?: () => number;
  _doors_discovered?: () => number;
  _run_elapsed_ms?: () => number;
  _lighter_value?: () => number;
  _lighter_set?: (on: number) => void;
  _web_files_found?: () => number;
  _file_code?: (index: number) => number;
  _file_name?: (index: number) => number;
  _web_saves_made?: () => number;
  _game_reset?: () => void;
  _craft_recipe?: (index: number) => number;
  _replay_length?: () => number;
  _replay_x?: (index: number) => number;
  _replay_y?: (index: number) => number;
  UTF8ToString?: (ptr: number) => string;
};

declare global {
  interface Window {
    Module?: EmscriptenModule;
  }
}

export function createModuleOptions(canvas: HTMLCanvasElement): EmscriptenModule {
  return {
    canvas,
    print: (...args: unknown[]) => console.log("[stdout]:", ...args),
    printErr: (...args: unknown[]) => console.error("[stderr]:", ...args),
  };
}

function module(): EmscriptenModule | undefined {
  return typeof window === "undefined" ? undefined : window.Module;
}

export function uiRevision(): number {
  return module()?._ui_revision?.() ?? 0;
}

export type GameSnapshot = {
  revision: number;
  cols: number;
  rows: number;
  cellX: number;
  cellY: number;
  walking: boolean;
  steps: number;
  revealed: number;
  cellPx: number;
  inventory: number[];
  hasLighter: boolean;
  lighterOn: boolean;
  health: number;
  itemsCollected: number;
  doorsDiscovered: number;
  filesFound: number;
  saves: number;
};

const EMPTY_INVENTORY = [-1, -1, -1, -1, -1, -1, -1, -1];

export function readSnapshot(revision: number): GameSnapshot {
  const m = module();
  if (!m) {
    return {
      revision,
      cols: 0,
      rows: 0,
      cellX: 0,
      cellY: 0,
      walking: false,
      steps: 0,
      revealed: 0,
      cellPx: 0,
      inventory: EMPTY_INVENTORY,
      hasLighter: false,
      lighterOn: false,
      health: 0,
      itemsCollected: 0,
      doorsDiscovered: 0,
      filesFound: 0,
      saves: 0,
    };
  }
  const count = m._web_inventory_count?.() ?? 0;
  const inventory = EMPTY_INVENTORY.slice();
  for (let i = 0; i < count; i++) {
    inventory[i] = m._web_inventory_slot?.(i) ?? -1;
  }
  return {
    revision,
    cols: m._grid_width?.() ?? 0,
    rows: m._grid_height?.() ?? 0,
    cellX: m._player_cell_x?.() ?? 0,
    cellY: m._player_cell_y?.() ?? 0,
    walking: (m._player_is_walking?.() ?? 0) !== 0,
    steps: m._player_total_steps?.() ?? 0,
    revealed: m._level_revealed?.() ?? 0,
    cellPx: m._level_cell_px?.() ?? 0,
    inventory,
    hasLighter: (m._web_inventory_has?.(7) ?? 0) !== 0,
    lighterOn: (m._lighter_value?.() ?? 0) !== 0,
    health: m._health_value?.() ?? 0,
    itemsCollected: m._items_collected?.() ?? 0,
    doorsDiscovered: m._doors_discovered?.() ?? 0,
    filesFound: m._web_files_found?.() ?? 0,
    saves: m._web_saves_made?.() ?? 0,
  };
}

export type FoundFile = { name: string; code: number };

export function readFiles(): FoundFile[] {
  const m = module();
  const count = m?._web_files_found?.() ?? 0;
  const files: FoundFile[] = [];
  for (let i = 0; i < count; i++) {
    const ptr = m?._file_name?.(i) ?? 0;
    const name = ptr && m?.UTF8ToString ? m.UTF8ToString(ptr) : "";
    files.push({ name, code: m?._file_code?.(i) ?? 0 });
  }
  return files;
}

export function setLighter(on: boolean): void {
  module()?._lighter_set?.(on ? 1 : 0);
}

export function gameReset(): void {
  module()?._game_reset?.();
}

export function craftRecipe(index: number): boolean {
  return (module()?._craft_recipe?.(index) ?? 0) !== 0;
}

export type ReplayPoint = { x: number; y: number };

export function readReplay(): ReplayPoint[] {
  const m = module();
  const count = m?._replay_length?.() ?? 0;
  const points: ReplayPoint[] = [];
  for (let i = 0; i < count; i++) {
    points.push({ x: m?._replay_x?.(i) ?? 0, y: m?._replay_y?.(i) ?? 0 });
  }
  return points;
}

export function runElapsedMs(): number {
  return module()?._run_elapsed_ms?.() ?? 0;
}

export const ITEM_NAMES = [
  "Empty Bottle",
  "Antique Coin",
  "Green Herb",
  "Ink Ribbon",
  "Screwdriver",
  "Med Injector",
  "Fuse",
  "Lighter",
  "Cherub Key",
];
