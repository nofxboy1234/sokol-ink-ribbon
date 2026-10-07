import { useEffect, useMemo, useState } from "react";
import {
  EMPTY_SLOT,
  INVENTORY_COLS,
  INVENTORY_ROWS,
  INVENTORY_SLOTS,
  type ItemId,
  inventoryOrigin,
  inventorySlots,
  mapCellPx,
} from "./wasmBridge";

type Layout = { cellPx: number; x: number; y: number };

type Edge = { key: string; left: number; top: number; horizontal: boolean };

/**
 * The five bands draw_glow_line stacks for one wall segment, widest first.
 * Each is a flat quad there, so each is a flat div here: the same widths and
 * alphas in the same order, which a single box-shadow cannot reproduce.
 */
const GLOW_LAYERS = [
  { width: 20, color: "rgba(38, 115, 255, 0.0225)" },
  { width: 11, color: "rgba(51, 140, 255, 0.045)" },
  { width: 6, color: "rgba(71, 166, 255, 0.09)" },
  { width: 3, color: "rgba(107, 199, 255, 0.2)" },
  { width: 1.4, color: "rgba(179, 235, 255, 0.425)" },
];

/**
 * One container per grid edge, so each line is drawn exactly once the same way
 * the map draws one segment per wall edge. Shared edges are not overdrawn.
 */
function gridEdges(cellPx: number): Edge[] {
  const edges: Edge[] = [];
  // horizontal edges: one per cell column, on every cell row boundary
  for (let row = 0; row <= INVENTORY_ROWS; row++) {
    for (let col = 0; col < INVENTORY_COLS; col++) {
      edges.push({
        key: `h${row}:${col}`,
        left: col * cellPx,
        top: row * cellPx,
        horizontal: true,
      });
    }
  }
  // vertical edges: one per cell row, on every cell column boundary
  for (let col = 0; col <= INVENTORY_COLS; col++) {
    for (let row = 0; row < INVENTORY_ROWS; row++) {
      edges.push({
        key: `v${col}:${row}`,
        left: col * cellPx,
        top: row * cellPx,
        horizontal: false,
      });
    }
  }
  return edges;
}

function sameSlots(a: ItemId[], b: ItemId[]): boolean {
  return a.length === b.length && a.every((item, i) => item === b[i]);
}

export function Inventory() {
  const [slots, setSlots] = useState<ItemId[]>(() =>
    Array.from({ length: INVENTORY_SLOTS }, () => EMPTY_SLOT),
  );
  const [layout, setLayout] = useState<Layout>({ cellPx: 0, x: 0, y: 0 });

  // the inventory lives in the wasm module, so poll it once per frame
  useEffect(() => {
    let frame = 0;
    const poll = () => {
      const next = inventorySlots();
      setSlots((prev) => (sameSlots(prev, next) ? prev : next));
      setLayout((prev) => {
        const cellPx = mapCellPx();
        const { x, y } = inventoryOrigin();
        return prev.cellPx === cellPx && prev.x === x && prev.y === y ? prev : { cellPx, x, y };
      });
      frame = requestAnimationFrame(poll);
    };
    frame = requestAnimationFrame(poll);
    return () => cancelAnimationFrame(frame);
  }, []);

  const edges = useMemo(() => gridEdges(layout.cellPx), [layout.cellPx]);

  // stay hidden until the canvas has reported its cell size
  if (layout.cellPx <= 0) {
    return null;
  }

  return (
    <div
      className="inventory"
      style={{
        left: layout.x,
        top: layout.y,
        width: INVENTORY_COLS * layout.cellPx,
        height: INVENTORY_ROWS * layout.cellPx,
      }}
    >
      {slots.map((item, slot) => (
        <div
          key={slot}
          className="inventory-cell"
          style={{
            left: (slot % INVENTORY_COLS) * layout.cellPx,
            top: Math.floor(slot / INVENTORY_COLS) * layout.cellPx,
            width: layout.cellPx,
            height: layout.cellPx,
          }}
        >
          {item !== EMPTY_SLOT && <span className="inventory-item" data-item={item} />}
        </div>
      ))}
      {edges.map((edge) => (
        <span
          key={edge.key}
          className="inventory-edge"
          data-axis={edge.horizontal ? "x" : "y"}
          style={{
            left: edge.left,
            top: edge.top,
            width: edge.horizontal ? layout.cellPx : 0,
            height: edge.horizontal ? 0 : layout.cellPx,
          }}
        >
          {GLOW_LAYERS.map((layer) => (
            <span
              key={layer.width}
              className="inventory-glow"
              style={{
                background: layer.color,
                ...(edge.horizontal
                  ? { width: "100%", height: layer.width }
                  : { width: layer.width, height: "100%" }),
              }}
            />
          ))}
        </span>
      ))}
    </div>
  );
}
