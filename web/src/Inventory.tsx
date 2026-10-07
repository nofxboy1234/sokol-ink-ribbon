import { useEffect, useMemo, useRef, useState } from "react";
import {
  EMPTY_SLOT,
  INVENTORY_COLS,
  INVENTORY_ROWS,
  INVENTORY_SLOTS,
  type ItemId,
  inventorySlots,
  mapCellPx,
} from "./wasmBridge";

// The pane needs no explicit width: it is flex-basis auto, so it sizes to this
// grid, which is exactly INVENTORY_COLS * cellPx wide. The grid's cell size
// comes from the camera, which fits the map to the canvas in the other pane,
// so the two settle on the same scale without any JS coordination.

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

const EMPTY_SLOTS: ItemId[] = Array.from({ length: INVENTORY_SLOTS }, () => EMPTY_SLOT);

export function Inventory() {
  const [slots, setSlots] = useState<ItemId[]>(EMPTY_SLOTS);
  const [cellPx, setCellPx] = useState(0);

  // The inventory lives in the wasm module and is polled once per frame, but
  // it almost never changes between frames. Compare against the last committed
  // values held in refs and only call setState on a real change, so a steady
  // state renders once rather than sixty times a second. Refs are read and
  // written in the same rAF callback, so no re-render is needed to see them.
  const slotsRef = useRef(slots);
  const cellPxRef = useRef(cellPx);
  useEffect(() => {
    let frame = 0;
    const poll = () => {
      const nextSlots = inventorySlots();
      if (!sameSlots(slotsRef.current, nextSlots)) {
        slotsRef.current = nextSlots;
        setSlots(nextSlots);
      }
      const nextCellPx = mapCellPx();
      if (nextCellPx !== cellPxRef.current) {
        cellPxRef.current = nextCellPx;
        setCellPx(nextCellPx);
      }
      frame = requestAnimationFrame(poll);
    };
    frame = requestAnimationFrame(poll);
    return () => cancelAnimationFrame(frame);
  }, []);

  const edges = useMemo(() => gridEdges(cellPx), [cellPx]);

  // stay hidden until the canvas has reported its cell size
  if (cellPx <= 0) {
    return null;
  }

  return (
    <div
      className="inventory"
      style={{
        width: INVENTORY_COLS * cellPx,
        height: INVENTORY_ROWS * cellPx,
      }}
    >
      {slots.map((item, slot) => (
        <div
          key={slot}
          className="inventory-cell"
          style={{
            left: (slot % INVENTORY_COLS) * cellPx,
            top: Math.floor(slot / INVENTORY_COLS) * cellPx,
            width: cellPx,
            height: cellPx,
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
            width: edge.horizontal ? cellPx : 0,
            height: edge.horizontal ? 0 : cellPx,
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
