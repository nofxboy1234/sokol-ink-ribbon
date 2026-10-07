/** Layout of Grace's inventory, mirroring native/src/inventory.h. */
export const INVENTORY_COLS = 4;
export const INVENTORY_ROWS = 2;
export const INVENTORY_SLOTS = INVENTORY_COLS * INVENTORY_ROWS;

/** An item id the player is currently carrying. */
export type ItemId = number;

export type EmscriptenModule = {
  canvas?: HTMLCanvasElement;
  print?: (...args: unknown[]) => void;
  printErr?: (...args: unknown[]) => void;
  /** native/src/inventory.c */
  _inventory_slot?: (slot: number) => number;
  /** native/src/camera.c */
  _camera_cell_px?: () => number;
  _camera_inventory_x?: () => number;
  _camera_inventory_y?: () => number;
};

declare global {
  interface Window {
    Module?: EmscriptenModule;
  }
}

/** Sentinel stored in an empty inventory slot (matches native `inventory_slot`). */
export const EMPTY_SLOT = -1;

/**
 * The wasm module loads asynchronously, so every accessor treats a missing
 * export as "nothing to show" rather than throwing.
 */
function module(): EmscriptenModule | undefined {
  return typeof window === "undefined" ? undefined : window.Module;
}

/** Item in the given slot, or EMPTY_SLOT when empty or not yet loaded. */
export function inventorySlot(slot: number): ItemId {
  return module()?._inventory_slot?.(slot) ?? EMPTY_SLOT;
}

/** Every slot, in row-major order (index 0 is the top-left cell). */
export function inventorySlots(): ItemId[] {
  return Array.from({ length: INVENTORY_SLOTS }, (_, slot) => inventorySlot(slot));
}

/** On-screen size of one map cell in CSS px; 0 until the canvas has sized. */
export function mapCellPx(): number {
  return module()?._camera_cell_px?.() ?? 0;
}

/** Top-left corner of the reserved inventory area, in CSS px. */
export function inventoryOrigin(): { x: number; y: number } {
  return {
    x: module()?._camera_inventory_x?.() ?? 0,
    y: module()?._camera_inventory_y?.() ?? 0,
  };
}
