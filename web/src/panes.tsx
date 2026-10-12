import { useEffect, useMemo, useRef, useState, type MouseEvent } from "react";
import {
  ITEM_NAMES,
  SAVE_SLOTS,
  clearInventorySlot,
  craftRecipe,
  gameReset,
  loadGame,
  moveInventorySlot,
  readFiles,
  readReplay,
  runElapsedMs,
  saveGame,
  saveTimeMs,
  saveUsed,
  setLighter,
} from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";

export function formatMs(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

// Same colours as item_color_rgb in native/src/level.c.
const ITEM_COLORS = [
  "#0008ff",
  "#bbbbbb",
  "#00ff26",
  "#cc0066",
  "#ff00ee",
  "#00cc26",
  "#00ffbb",
  "#f2f200",
  "#e60026",
];

// The map page unmounts on the way to Records, but the wasm game stays
// alive, so this has to live outside RunPanel or each visit posts again.
let goalRunSaved = false;

export function RunPanel() {
  const elapsed = useElapsed();
  const { goalMet } = useGameState();
  const [status, setStatus] = useState("");

  // A run saves itself the moment the goal (the fuse box) is activated.
  useEffect(() => {
    if (!goalMet) {
      goalRunSaved = false;
      return;
    }
    if (goalRunSaved) {
      return;
    }
    goalRunSaved = true;
    const replay = readReplay();
    void (async () => {
      try {
        const response = await fetch("/runs", {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify({
            levelId: "care-center-01",
            durationMs: Math.round(runElapsedMs()),
            completed: true,
            replay,
          }),
        });
        setStatus(
          response.ok ? "Run saved" : response.status === 401 ? "Sign in to save" : "Save failed",
        );
      } catch {
        setStatus("Save failed");
      }
    })();
  }, [goalMet]);

  return (
    <div className="run-panel">
      <span className="timer">{formatMs(elapsed)}</span>
      {status && <span className="run-status">{status}</span>}
    </div>
  );
}

const HEALTH_LABELS = ["FINE", "CAUTION", "DANGER"];
const HEALTH_COLORS = ["var(--green)", "var(--orange)", "var(--red)"];

// Same shapes as item_icon_shape in native/src/level.c.
const ITEM_SHAPES = [0, 0, 0, 3, 2, 3, 0, 1, 3];

const ITEM_TEXT = [
  "A clear glass bottle. It could hold a mixed herb.",
  "A worn coin stamped with a crest. It has no spendable value here.",
  "A fresh green herb. It can be ground, or used to steady Grace.",
  "A typewriter ribbon. Saving at a typewriter spends one.",
  "A small flat-head screwdriver. It pries panels and tightens fittings.",
  "A prepared dose in a glass syringe. It is meant for an emergency.",
  "A glass cartridge fuse. The basement fuse box will take it.",
  "A pocket lighter. It throws a small circle of light when it is lit.",
  "A key topped with a small cherub. It opens a matching lock.",
];

type ItemAction = "Move" | "Use" | "Discard";

const ITEM_ACTIONS: ItemAction[][] = [
  ["Discard"],
  ["Move", "Discard"],
  ["Move", "Use", "Discard"],
  ["Move", "Discard"],
  ["Move"],
  ["Move", "Use", "Discard"],
  ["Move"],
  [],
  ["Move"],
];

const STEROID_CHARGES = 0;
const STABILIZER_CHARGES = 0;

function ItemGlyph({ item }: { item: number }) {
  const color = ITEM_COLORS[item] ?? "#ff006a";
  const shape = ITEM_SHAPES[item] ?? 0;
  if (shape === 1) {
    return <span className="glyph glyph-square" style={{ background: color }} />;
  }
  if (shape === 2) {
    return <span className="glyph glyph-ring" style={{ borderColor: color }} />;
  }
  if (shape === 3) {
    return (
      <span className="glyph glyph-ink" style={{ background: color }}>
        <span className="glyph-hole" />
      </span>
    );
  }
  return <span className="glyph glyph-circle" style={{ background: color }} />;
}

function ChargePips({
  label,
  icon,
  filled,
  fill,
}: {
  label: string;
  icon: string;
  filled: number;
  fill: string;
}) {
  return (
    <div className="pip-group" title={label}>
      <span className="pip" style={{ background: icon }} />
      {Array.from({ length: 4 }, (_, index) => (
        <span
          key={index}
          className="pip"
          style={{ background: index < filled ? fill : undefined }}
        />
      ))}
    </div>
  );
}

export function Inventory() {
  const { inventory, health, hasLighter, lighterOn } = useGameState();
  const [selected, setSelected] = useState(-1);
  const [movingFrom, setMovingFrom] = useState(-1);
  const [menu, setMenu] = useState<{ index: number; x: number; y: number } | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!menu) {
      return;
    }
    const onDown = (event: globalThis.MouseEvent) => {
      if (menuRef.current?.contains(event.target as Node)) {
        return;
      }
      setMenu(null);
    };
    window.addEventListener("mousedown", onDown);
    return () => window.removeEventListener("mousedown", onDown);
  }, [menu]);

  useEffect(() => {
    if (!menu && movingFrom < 0) {
      return;
    }
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== "Escape") {
        return;
      }
      event.stopImmediatePropagation();
      setMenu(null);
      setMovingFrom(-1);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [menu, movingFrom]);

  const selectedItem = selected >= 0 ? inventory[selected] : -1;
  const menuItem = menu ? inventory[menu.index] : -1;
  const menuActions = menuItem >= 0 ? (ITEM_ACTIONS[menuItem] ?? []) : [];

  const onSlotClick = (index: number) => {
    if (movingFrom >= 0) {
      if (movingFrom !== index) {
        moveInventorySlot(movingFrom, index);
        setSelected(index);
      }
      setMovingFrom(-1);
      return;
    }
    setSelected((inventory[index] ?? -1) >= 0 ? index : -1);
  };

  const onSlotMenu = (event: MouseEvent, index: number) => {
    event.preventDefault();
    if (movingFrom >= 0) {
      setMovingFrom(-1);
      return;
    }
    const item = inventory[index] ?? -1;
    if (item < 0 || (ITEM_ACTIONS[item] ?? []).length === 0) {
      setMenu(null);
      return;
    }
    setSelected(index);
    setMenu({
      index,
      x: Math.min(event.clientX, window.innerWidth - 120),
      y: Math.min(event.clientY, window.innerHeight - 110),
    });
  };

  const runAction = (action: ItemAction) => {
    if (!menu) {
      return;
    }
    const index = menu.index;
    setMenu(null);
    if (action === "Move") {
      setMovingFrom(index);
      return;
    }
    if (action === "Discard") {
      clearInventorySlot(index);
      if (selected === index) {
        setSelected(-1);
      }
    }
  };

  return (
    <div className="pane-content">
      <h2>ITEMS</h2>
      <div className="inventory-grid">
        {inventory.map((item, index) => (
          <button
            key={index}
            type="button"
            className="inventory-slot"
            data-filled={item >= 0}
            data-selected={selected === index}
            data-moving={movingFrom === index}
            title={item >= 0 ? ITEM_NAMES[item] : ""}
            onClick={() => onSlotClick(index)}
            onContextMenu={(event) => onSlotMenu(event, index)}
          >
            {item >= 0 && <ItemGlyph item={item} />}
            {item >= 0 && <span className="inventory-name">{ITEM_NAMES[item]}</span>}
          </button>
        ))}
      </div>
      {movingFrom >= 0 && <p className="move-hint">Click a slot</p>}
      <div className="item-detail">
        {selectedItem >= 0 && (
          <>
            <h3>{ITEM_NAMES[selectedItem]}</h3>
            <p>{ITEM_TEXT[selectedItem]}</p>
          </>
        )}
      </div>
      <div className="equip-row">
        <div className="gun-slot" title="No gun equipped" />
        <div className="quick-slots">
          <div className="quick-slot" />
          {hasLighter ? (
            <button
              type="button"
              className="quick-slot quick-slot-btn"
              data-on={lighterOn}
              title={lighterOn ? "Lighter on" : "Lighter"}
              onClick={() => setLighter(!lighterOn)}
            >
              <span className="lighter-swatch" />
            </button>
          ) : (
            <div className="quick-slot" />
          )}
        </div>
        <div className="status-stack">
          <div className="pip-row">
            <ChargePips
              label="Steroids Injector. Increases health."
              icon="#f2ff00"
              filled={STEROID_CHARGES}
              fill="#f2ff00"
            />
            <ChargePips
              label="Stabilizer Injector. Increases gun damage and handling."
              icon="#66d5f4"
              filled={STABILIZER_CHARGES}
              fill="#66d5f4"
            />
          </div>
          <div
            className="status-color"
            style={{ background: HEALTH_COLORS[health] ?? HEALTH_COLORS[0] }}
          />
          <div className="status-label">{HEALTH_LABELS[health] ?? HEALTH_LABELS[0]}</div>
        </div>
      </div>
      {movingFrom >= 0 && (
        <button type="button" className="move-catch" onClick={() => setMovingFrom(-1)} />
      )}
      {menu && menuActions.length > 0 && (
        <div ref={menuRef} className="item-menu" style={{ left: menu.x, top: menu.y }}>
          {menuActions.map((action) => (
            <button key={action} type="button" onClick={() => runAction(action)}>
              {action}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

const RECIPES = [
  { inputs: ["Green Herb", "Green Herb"], output: "Mixed Herb" },
  { inputs: ["Green Herb", "Empty Bottle"], output: "Herb Bottle" },
  { inputs: ["Fuse", "Screwdriver"], output: "Repaired Fuse" },
];

export function Crafting() {
  const { inventory } = useGameState();
  return (
    <div className="pane-content">
      <h2>CRAFTING</h2>
      <ul className="recipe-list">
        {RECIPES.map((recipe, index) => (
          <li key={recipe.output}>
            <button
              type="button"
              className="recipe"
              disabled={inventory.every((item) => item < 0)}
              onClick={() => craftRecipe(index)}
            >
              <span className="recipe-dot" style={{ background: "#ff006a" }} />
              <span>{recipe.inputs[0]}</span>
              <span className="recipe-op">+</span>
              <span className="recipe-ring" />
              <span>{recipe.inputs[1]}</span>
              <span className="recipe-op">→</span>
              <span className="recipe-dot" style={{ background: "#ff006a" }} />
              <span className="recipe-output">{recipe.output}</span>
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}

export function Files() {
  const { filesFound } = useGameState();
  const files = useMemo(() => readFiles(), [filesFound]);
  return (
    <div className="pane-content">
      <h2>FILES</h2>
      <ul className="file-list">
        {files.length === 0 && <li className="file file-empty">NO DATA</li>}
        {files.map((file) => (
          <li key={file.name} className="file">
            <span className="file-name">{file.name}</span>
            <span className="file-code">= {file.code}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

export function PauseMenu({
  open,
  onClose,
  onLoad,
}: {
  open: boolean;
  onClose: () => void;
  onLoad: () => void;
}) {
  if (!open) {
    return null;
  }
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="pause-menu" onClick={(event) => event.stopPropagation()}>
        <button
          type="button"
          className="pause-item pause-selected"
          onClick={() => {
            gameReset();
            onClose();
          }}
        >
          Restart
        </button>
        <button type="button" className="pause-item" onClick={onLoad}>
          Load Game
        </button>
        <button type="button" className="pause-item" onClick={onClose}>
          Resume
        </button>
      </div>
    </div>
  );
}

// Save/Load menu in the same style as the pause menu (ref/map.kra "Load menu").
export function SaveLoadMenu({ open, onClose }: { open: boolean; onClose: () => void }) {
  if (!open) {
    return null;
  }
  // Remount fresh each time the menu opens. saveUsed/saveTimeMs read mutable
  // wasm state that React can't observe, so the React Compiler would otherwise
  // memoize the reads and show stale slot data after a save.
  return <SaveLoadSlots onClose={onClose} />;
}

type SaveSlotInfo = { index: number; used: boolean; time: number };

function SaveLoadSlots({ onClose }: { onClose: () => void }) {
  const slots = useMemo<SaveSlotInfo[]>(
    () =>
      Array.from({ length: SAVE_SLOTS }, (_, index) => ({
        index,
        used: saveUsed(index),
        time: saveTimeMs(index),
      })),
    [],
  );
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="save-menu" onClick={(event) => event.stopPropagation()}>
        <div className="save-title">Save / Load</div>
        {slots.map((slot) => (
          <div key={slot.index} className="save-row">
            <span className="save-slot">Slot {slot.index + 1}</span>
            <span className="save-data">{slot.used ? formatMs(slot.time) : "NO DATA"}</span>
            <button
              type="button"
              className="pause-item save-btn"
              onClick={() => {
                saveGame(slot.index);
                onClose();
              }}
            >
              Save
            </button>
            <button
              type="button"
              className="pause-item save-btn"
              disabled={!slot.used}
              onClick={() => {
                loadGame(slot.index);
                onClose();
              }}
            >
              Load
            </button>
          </div>
        ))}
        <button type="button" className="pause-item" onClick={onClose}>
          Close
        </button>
      </div>
    </div>
  );
}
