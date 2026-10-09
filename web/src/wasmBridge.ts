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
  _run_elapsed_ms?: () => number;
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
  health: number;
  itemsCollected: number;
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
      health: 0,
      itemsCollected: 0,
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
    health: m._health_value?.() ?? 0,
    itemsCollected: m._items_collected?.() ?? 0,
  };
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
