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
  _web_inventory_move?: (from: number, to: number) => number;
  _web_inventory_clear?: (index: number) => number;
  _web_inventory_use?: (index: number) => number;
  _web_enemy_state?: () => number;
  _web_grace_mode?: () => number;
  _web_grace_can_pause?: () => number;
  _web_grace_action?: (action: number) => number;
  _web_grace_screen_x?: () => number;
  _web_grace_screen_y?: () => number;
  _web_cell_css_px?: () => number;
  _web_enemy_px?: () => number;
  _web_enemy_py?: () => number;
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
  _web_goal_met?: () => number;
  _game_reset?: () => void;
  _craft_recipe?: (index: number) => number;
  _web_crafting_count?: () => number;
  _web_crafting_part_item?: (index: number, part: number) => number;
  _web_crafting_part_qty?: (index: number, part: number) => number;
  _web_crafting_ready?: (index: number) => number;
  _web_item_count?: () => number;
  _web_item_label?: (item: number) => number;
  _web_item_color?: (item: number) => number;
  _web_item_icon?: (item: number) => number;
  _replay_length?: () => number;
  _replay_x?: (index: number) => number;
  _replay_y?: (index: number) => number;
  _set_paused?: (paused: number) => void;
  _take_save_menu?: () => number;
  _save_game?: (slot: number) => number;
  _load_game?: (slot: number) => number;
  _save_used?: (slot: number) => number;
  _save_time?: (slot: number) => number;
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
  goalMet: boolean;
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
      goalMet: false,
    };
  }
  const inventory = EMPTY_INVENTORY.slice();
  for (let i = 0; i < inventory.length; i++) {
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
    goalMet: (m._web_goal_met?.() ?? 0) !== 0,
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

// Mirrors grace_mode_t / grace_action_t in native/src/main.c.
export const GraceMode = { Paused: 0, Menu: 1, Target: 2, Moving: 3, Waiting: 4 } as const;
export const GraceAction = { Walk: 0, Run: 1, Wait: 2, Pause: 3, Cancel: 4 } as const;

export type GraceView = { mode: number; canPause: boolean; x: number; y: number; cell: number };

export function readGrace(): GraceView {
  const m = module();
  return {
    mode: m?._web_grace_mode?.() ?? GraceMode.Paused,
    canPause: (m?._web_grace_can_pause?.() ?? 0) !== 0,
    x: m?._web_grace_screen_x?.() ?? 0,
    y: m?._web_grace_screen_y?.() ?? 0,
    cell: m?._web_cell_css_px?.() ?? 0,
  };
}

export function graceAction(action: number): boolean {
  return (module()?._web_grace_action?.(action) ?? 0) !== 0;
}

export function setPaused(paused: boolean): void {
  module()?._set_paused?.(paused ? 1 : 0);
}

export const SAVE_SLOTS = 6;

export function takeSaveMenu(): boolean {
  return (module()?._take_save_menu?.() ?? 0) !== 0;
}

export function saveGame(slot: number): boolean {
  return (module()?._save_game?.(slot) ?? 0) !== 0;
}

export function loadGame(slot: number): boolean {
  return (module()?._load_game?.(slot) ?? 0) !== 0;
}

export function saveUsed(slot: number): boolean {
  return (module()?._save_used?.(slot) ?? 0) !== 0;
}

export function saveTimeMs(slot: number): number {
  return module()?._save_time?.(slot) ?? 0;
}

export function moveInventorySlot(from: number, to: number): boolean {
  return (module()?._web_inventory_move?.(from, to) ?? 0) !== 0;
}

export function clearInventorySlot(index: number): boolean {
  return (module()?._web_inventory_clear?.(index) ?? 0) !== 0;
}

export function applyInventoryItem(index: number): boolean {
  return (module()?._web_inventory_use?.(index) ?? 0) !== 0;
}

export function craftRecipe(index: number): boolean {
  return (module()?._craft_recipe?.(index) ?? 0) !== 0;
}

function copyCString(ptr: number): string {
  const utf8 = module()?.UTF8ToString;
  if (!ptr || !utf8) {
    return "";
  }
  return utf8(ptr);
}

export type RecipePart = {
  item: number;
  qty: number;
  label: string;
};

export type RecipeView = {
  inputs: RecipePart[];
  output: RecipePart;
};

export type ItemAppearance = {
  label: string;
  color: string;
  shape: number;
};

export function itemCount(): number {
  return module()?._web_item_count?.() ?? 0;
}

export function itemAppearance(item: number): ItemAppearance {
  const m = module();
  const label = copyCString(m?._web_item_label?.(item) ?? 0);
  const color = copyCString(m?._web_item_color?.(item) ?? 0);
  return {
    label: label || "Item",
    color: color || "#1e1e1e",
    shape: m?._web_item_icon?.(item) ?? 0,
  };
}

export function readRecipes(): RecipeView[] {
  const m = module();
  if (!m?._web_crafting_count) {
    return [];
  }
  const recipes: RecipeView[] = [];
  const count = m._web_crafting_count();
  for (let index = 0; index < count; index++) {
    const inputs: RecipePart[] = [];
    for (let part = 0; ; part++) {
      const item = m._web_crafting_part_item?.(index, part) ?? -1;
      if (item < 0) {
        break;
      }
      const qty = m._web_crafting_part_qty?.(index, part) ?? 0;
      inputs.push({ item, qty, label: itemAppearance(item).label });
    }
    const outItem = m._web_crafting_part_item?.(index, -1) ?? -1;
    if (inputs.length === 0 || outItem < 0) {
      continue;
    }
    const outQty = m._web_crafting_part_qty?.(index, -1) ?? 0;
    recipes.push({
      inputs,
      output: { item: outItem, qty: outQty, label: itemAppearance(outItem).label },
    });
  }
  return recipes;
}

export function recipeReady(index: number): boolean {
  return (module()?._web_crafting_ready?.(index) ?? 0) !== 0;
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
