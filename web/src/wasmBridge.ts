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
  _web_grace_setup?: () => number;
  _web_grace_ready?: () => number;
  _web_grace_menu_open?: () => number;
  _web_grace_last?: () => number;
  _web_grace_stance?: () => number;
  _web_grace_pending?: () => number;
  _web_route_length?: () => number;
  _web_quick_open?: () => number;
  _web_quick_select?: (slot: number) => number;
  _web_bottle_equipped?: () => number;
  _web_bottle_active?: () => number;
  _web_bottle_empty?: () => number;
  _web_enemy_angry?: () => number;
  _web_light_at?: (x: number, y: number) => number;
  _web_bottle_equip?: (on: number) => number;
  _web_throw_hit_chance?: () => number;
  _web_throw_allowed?: () => number;
  _web_throw_in_reach?: (x: number, y: number) => number;
  _web_cell_floor?: (x: number, y: number) => number;
  _web_enemy_flash?: () => number;
  _web_grace_flash?: () => number;
  _web_enemy_focus_x?: () => number;
  _web_enemy_focus_y?: () => number;
  _web_impact_age_ms?: () => number;
  _web_test_seed?: (seed: number) => void;
  _web_test_anger?: () => void;
  _web_test_enemy_place?: (x100: number, y100: number) => void;
  _web_enemy_screen_x?: () => number;
  _web_enemy_screen_y?: () => number;
  _web_aim_enemy?: () => number;
  _web_stick_input?: (x: number, y: number) => void;
  _web_aim_input?: (x: number, y: number) => void;
  _web_aim_toggle?: () => number;
  _web_aiming?: () => number;
  _web_interact_trigger?: () => number;
  _web_grace_stress_ms?: () => number;
  _web_grace_hurt_ms?: () => number;
  _web_item_visible_at?: (x: number, y: number) => number;
  _web_test_give?: (item: number) => number;
  _web_test_enemy_at?: (x: number, y: number) => void;
  _web_test_hurt?: () => void;
  _web_test_grace_at?: (x: number, y: number) => void;
  _web_camera_pan?: (dx: number, dy: number) => void;
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
  _replay_t?: (index: number) => number;
  _web_level_complete?: () => number;
  _web_grace_dead?: () => number;
  _web_last_save_slot?: () => number;
  _web_inventory_capacity?: () => number;
  _web_crafting_known?: (index: number) => number;
  _web_on_top?: () => number;
  _web_pending_safe?: () => number;
  _web_try_safe_code?: (code: number) => number;
  _web_pending_code_door?: () => number;
  _web_try_door_code?: (code: number) => number;
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
  levelComplete: boolean;
  dead: boolean;
  lastSaveSlot: number;
  capacity: number;
  onTop: boolean;
  bottleEquipped: boolean;
  bottleEmpty: boolean;
};

const EMPTY_INVENTORY = Array.from({ length: 16 }, () => -1);

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
      levelComplete: false,
      dead: false,
      lastSaveSlot: -1,
      capacity: 8,
      onTop: false,
      bottleEquipped: false,
      bottleEmpty: false,
    };
  }
  const capacity = m._web_inventory_capacity?.() ?? 8;
  const inventory = EMPTY_INVENTORY.slice(0, capacity);
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
    levelComplete: (m._web_level_complete?.() ?? 0) !== 0,
    dead: (m._web_grace_dead?.() ?? 0) !== 0,
    lastSaveSlot: m._web_last_save_slot?.() ?? -1,
    capacity: m._web_inventory_capacity?.() ?? 8,
    onTop: (m._web_on_top?.() ?? 0) !== 0,
    bottleEquipped: (m._web_bottle_equipped?.() ?? 0) !== 0,
    bottleEmpty: (m._web_bottle_empty?.() ?? 0) !== 0,
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

export function gameReset(): void {
  module()?._game_reset?.();
}

// Mirrors grace_mode_t / grace_setup_t / grace_action_t in native/src/main.c.
export const GraceMode = { Paused: 0, Walking: 1, Running: 2, Waiting: 3, Hurt: 4 } as const;
export const GraceSetup = { None: 0, Walk: 1, Run: 2, Wait: 3, Throw: 4 } as const;
export const GraceAction = {
  Walk: 0,
  Run: 1,
  Wait: 2,
  Ok: 3,
  Cancel: 4,
  Pause: 5,
  Throw: 6,
} as const;
// Mirrors throw_stance_t in native/src/throw.h.
export const ThrowStance = { Wait: 0, Walk: 1, Run: 2 } as const;
// The item ordinal of the Empty Bottle (item_t in native/src/level.h).
export const ITEM_BOTTLE = 0;
export const ITEM_LIGHTER = 7;

export type GraceView = {
  // false until the wasm map is up and Grace has a real screen position
  live: boolean;
  mode: number;
  setup: number;
  ready: boolean;
  menuOpen: boolean;
  // her last action (a GraceMode other than Paused), shown while Paused
  last: number;
  quickOpen: boolean;
  aiming: boolean;
  bottleEquipped: boolean;
  bottleActive: boolean;
  bottleEmpty: boolean;
  hasLighter: boolean;
  lighterOn: boolean;
  throwAllowed: boolean;
  stance: number;
  hitChance: number;
  x: number;
  y: number;
  cell: number;
};

export function readGrace(): GraceView {
  const m = module();
  const cell = m?._web_cell_css_px?.() ?? 0;
  return {
    live: (m?._grid_width?.() ?? 0) > 0 && cell > 0,
    mode: m?._web_grace_mode?.() ?? GraceMode.Paused,
    menuOpen: (m?._web_grace_menu_open?.() ?? 0) !== 0,
    last: m?._web_grace_last?.() ?? GraceMode.Waiting,
    quickOpen: (m?._web_quick_open?.() ?? 0) !== 0,
    aiming: (m?._web_aiming?.() ?? 0) !== 0,
    bottleEquipped: (m?._web_bottle_equipped?.() ?? 0) !== 0,
    bottleActive: (m?._web_bottle_active?.() ?? 0) !== 0,
    bottleEmpty: (m?._web_bottle_empty?.() ?? 0) !== 0,
    hasLighter: (m?._web_inventory_has?.(ITEM_LIGHTER) ?? 0) !== 0,
    lighterOn: (m?._lighter_value?.() ?? 0) !== 0,
    throwAllowed: (m?._web_throw_allowed?.() ?? 0) !== 0,
    stance: m?._web_grace_stance?.() ?? ThrowStance.Wait,
    hitChance: m?._web_throw_hit_chance?.() ?? 0,
    setup: m?._web_grace_setup?.() ?? GraceSetup.None,
    ready: (m?._web_grace_ready?.() ?? 0) !== 0,
    x: m?._web_grace_screen_x?.() ?? 0,
    y: m?._web_grace_screen_y?.() ?? 0,
    cell,
  };
}

export function graceAction(action: number): boolean {
  return (module()?._web_grace_action?.(action) ?? 0) !== 0;
}

// Quick Equip slots 0..3 are North, East, South, West.
export function quickSelect(slot: number): boolean {
  return (module()?._web_quick_select?.(slot) ?? 0) !== 0;
}

export function equipBottle(on: boolean): boolean {
  return (module()?._web_bottle_equip?.(on ? 1 : 0) ?? 0) !== 0;
}

// Analogue stick vector (-1..1); (0,0) is release.
export function stickInput(x: number, y: number): void {
  module()?._web_stick_input?.(x, y);
}

// Aim stick vector (-1..1); (0,0) is release (throws/cancels in C).
export function aimInput(x: number, y: number): void {
  module()?._web_aim_input?.(x, y);
}

// The aim stick is held but inside its dead zone. C tells this apart from
// a release by magnitude alone: exactly (0,0) on release throws/cancels,
// while a non-zero vector under its 0.12 gate just marks the stick back
// inside the dead zone. So the dead signal is a token sub-gate vector;
// it sets no aim direction or length (C ignores sub-gate vectors).
export function aimDeadZone(): void {
  module()?._web_aim_input?.(0.01, 0);
}

export function aimToggle(): boolean {
  return (module()?._web_aim_toggle?.() ?? 0) !== 0;
}

export function interactTrigger(): boolean {
  return (module()?._web_interact_trigger?.() ?? 0) !== 0;
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
  index: number;
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
    if ((m._web_crafting_known?.(index) ?? 1) === 0) continue;
    recipes.push({
      index,
      inputs,
      output: { item: outItem, qty: outQty, label: itemAppearance(outItem).label },
    });
  }
  return recipes;
}

export function recipeReady(index: number): boolean {
  return (module()?._web_crafting_ready?.(index) ?? 0) !== 0;
}

export type ReplayPoint = { x: number; y: number; t?: number };

export function readReplay(): ReplayPoint[] {
  const m = module();
  const count = m?._replay_length?.() ?? 0;
  const points: ReplayPoint[] = [];
  for (let i = 0; i < count; i++) {
    points.push({ x: m?._replay_x?.(i) ?? 0, y: m?._replay_y?.(i) ?? 0, t: m?._replay_t?.(i) ?? 0 });
  }
  return points;
}

export function runElapsedMs(): number {
  return module()?._run_elapsed_ms?.() ?? 0;
}

export function graceDead(): boolean { return (module()?._web_grace_dead?.() ?? 0) !== 0; }
export function levelComplete(): boolean { return (module()?._web_level_complete?.() ?? 0) !== 0; }
export function lastSaveSlot(): number { return module()?._web_last_save_slot?.() ?? -1; }
export function pendingSafe(): number { return module()?._web_pending_safe?.() ?? -1; }
export function trySafeCode(code: number): boolean { return (module()?._web_try_safe_code?.(code) ?? 0) !== 0; }
export function pendingCodeDoor(): number { return module()?._web_pending_code_door?.() ?? -1; }
export function tryDoorCode(code: number): boolean { return (module()?._web_try_door_code?.(code) ?? 0) !== 0; }
